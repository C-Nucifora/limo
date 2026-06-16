# Plan: Autoscan for installed games (+ non-standard prefixes)

Tracked in issue #236.

Goal: a one-click "scan my computer for games" flow that finds installed Steam games, highlights the ones Limo has a preset for, and adds them — with support for non-standard Steam library and Proton-prefix locations.

## What already exists (good news — ~70% built)

`ImportFromSteamDialog` (`src/ui/importfromsteamdialog.{h,cpp,ui}`) already:
- Auto-detects Steam at the native (`~/.steam/steam/steamapps`) and flatpak (`~/.var/app/com.valvesoftware.Steam/...`) locations (`init()`, lines ~28-34).
- Parses `libraryfolders.vdf` and scans **every** library folder's `steamapps/` for `appmanifest_*.acf` (`updateTable`, lines ~96-140) — so multi-drive libraries already work.
- Lists each game with name, app_id, **has-prefix** flag, and install path; detects the Proton prefix at `…/compatdata/<appid>/pfx/drive_c` (`addTableRow` line ~228, `on_buttonBox_accepted` line ~294).
- Lets the user **browse to a custom `steamapps` directory** (`on_pushButton_clicked` / `path_field`, line ~66) and re-scan — i.e. non-standard Steam locations are already handled.
- Emits `applicationImported(name, app_id, path, prefix_path, icon_path)` → `AddAppDialog::initConfigForApp(app_id)` looks up `<appid>.json` via `gameConfigSearchDirs()` and auto-builds deployers.

## What's new (the actual work)

### 1. "Has Limo preset" detection + filter (ties into #234)
- Lift `AddAppDialog::gameConfigSearchDirs()` (`addappdialog.cpp:206`) into a shared helper (free function in a util TU, or a `ModdedApplication`/`AddAppDialog` static) so the import dialog can call it too.
- In `addTableRow`, check whether `<app_id>.json` exists in those dirs → add a **"Limo preset" column** (✓ / blank) to `app_table` (programmatic `setColumnCount(4→5)` + header, no `.ui` change strictly required).
- Add a **"Only show games Limo supports"** checkbox that hides unsupported rows (reuse the existing `setRowHidden` filter logic from `on_search_field_textEdited`), and sort supported-first.

### 2. Autoscan + batch add
- A **"Scan for games"** action that (re)runs `updateTable` across all detected libraries + any user-added custom paths, then selects the "supported" filter.
- An **"Add all supported"** button: iterate the supported rows and emit `applicationImported` for each (or a new `applicationsImported(list)` signal) so `MainWindow` creates one app per game in a batch, skipping ones already added (dedupe by app_id against existing apps).
- Surface counts ("Found 12 games, 5 supported by Limo, 3 not yet added").

### 3. Non-standard Steam / game prefixes (the explicit ask)
- **Custom Steam libraries:** already browsable; additionally let the user **add multiple** custom library roots (persist them in settings) so the scan always includes non-default drives without re-browsing each time.
- **Per-game prefix override:** today `prefix_path` is auto-derived as `…/compatdata/<appid>/pfx/drive_c`. For games with a **non-standard prefix** (custom `STEAM_COMPAT_DATA_PATH`, a manually-created WINEPREFIX, or a non-Steam/Proton install), add an **editable prefix field / per-row "Browse prefix…"** so the user can point Limo at the correct `…/pfx/drive_c` (or any WINE prefix's `drive_c`). This value flows through `applicationImported`'s `prefix_path`, which is what `$STEAM_PREFIX_PATH$` resolves to in preset `target_dir`s — so overriding it makes presets resolve correctly for non-standard setups.
- Validate the chosen prefix (must contain `drive_c`) and warn if a preset uses `$STEAM_PREFIX_PATH$` but no prefix is set.

## Files touched
- `src/ui/importfromsteamdialog.{h,cpp,ui}` — preset column + filter + scan/add-all buttons + prefix-override field; optional `applicationsImported` batch signal.
- `src/ui/addappdialog.{h,cpp}` — lift `gameConfigSearchDirs()` to a shared/static helper (or move to a small `gameconfig` util).
- `src/ui/mainwindow.{h,cpp}` — handle the batch-import signal (create N apps, dedupe by app_id), and expose a top-level "Scan for games" entry point (menu/empty-state button) so the user can trigger it without going through Add-App first.
- Optional `src/core/` — persist user-added custom library roots in settings.

## Scope / phasing
- **Phase 1 (core ask):** preset column + "only supported" filter + "Add all supported" + per-game prefix override + a top-level "Scan for games" entry point.
- **Phase 2:** persist custom library roots; counts/summary; dedupe polish.
- Pairs with the supported-games UI work (#234) and the new presets (#229/#231 + the 26 added) — the more presets shipped, the more the autoscan surfaces.

## Verification
UI feature — needs manual run-through on a real Steam install (standard + a custom library + a non-standard prefix). The scan/parse logic can be unit-smoke-tested against fixture `libraryfolders.vdf` / `appmanifest` files.

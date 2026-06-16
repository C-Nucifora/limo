# Plan: Assetto Corsa support

Research: [`research/03-assetto-corsa.md`](research/03-assetto-corsa.md), [`research/04-limo-add-game-arch.md`](research/04-limo-add-game-arch.md).

## 1. How AC modding works

- **Assetto Corsa** (original) — Steam App ID **244210**, installdir `assettocorsa`. The mod-heavy target.
- **Assetto Corsa Competizione** (ACC) — App ID **805550** — a *different* model (liveries/setups under the prefix's `Documents/Assetto Corsa Competizione/Customs/`). Not the same drop-in tree; out of scope for the main preset (separate note in §5).
- Mods **merge into the install root** `$STEAM_INSTALL_PATH$` (`steamapps/common/assettocorsa/`). There is **no** separate mod dir — modded content lives intermixed with stock content:
  - `content/cars/<car_id>/` (and `…/skins/<skin>/` for liveries)
  - `content/tracks/<track_id>/`
  - `apps/python/<app>/`, `apps/lua/<app>/`
  - `content/gui/`, `content/sfx/`, `system/…`
  - `extension/` + a root `dwrite.dll` (Custom Shaders Patch — itself "a mod" in deploy terms)
- **No built-in mod manager, no load order: "installed = on disk."** A car/track is enabled iff its folder exists with a valid `ui_*.json`. This is the ideal shape for Limo's hard-link deploy: **deploying = enabling, un-deploying = disabling**.
- **No Content Manager / CSP integration required.** Limo only places files; CM/CSP/the game read Limo's hard-links transparently. (Leave CM's own user config under the prefix to CM.)

## 2. Deployer choice — no new C++ needed

A single **`Case Matching Deployer`** targeting the install root covers AC: it merges a staged `content/…` tree into the existing tree file-by-file, and `CaseMatchingDeployer` reconciles case across the target and other enabled mods (AC expects lowercase paths on case-sensitive Linux FS). Conflicts (two mods writing the same path — e.g. CSP `extension/config` `.ini`s, shared `content/gui`) surface in Limo's normal per-file conflict UI; deploy priority is the de-facto tiebreaker. No LOOT/plugin order needed.

### Proposed preset — `steam_app_configs/244210.json`

```json
{
  "name": "Assetto Corsa",
  "deployers": [
    {
      "type": "Case Matching Deployer",
      "name": "Content",
      "target_dir": "$STEAM_INSTALL_PATH$",
      "deploy_mode": "hard_link"
    }
  ],
  "auto_tags": [
    { "name": "Car",   "expression": "0", "conditions": [ { "condition_type": "file_name", "invert": false, "use_regex": false, "search_string": "ui_car.json" } ] },
    { "name": "Track", "expression": "0", "conditions": [ { "condition_type": "file_name", "invert": false, "use_regex": false, "search_string": "ui_track.json" } ] },
    { "name": "App",   "expression": "0or1", "conditions": [ { "condition_type": "path", "invert": false, "use_regex": false, "search_string": "apps/python/" }, { "condition_type": "path", "invert": false, "use_regex": false, "search_string": "apps/lua/" } ] },
    { "name": "CSP/Extension", "expression": "0or1", "conditions": [ { "condition_type": "file_name", "invert": false, "use_regex": false, "search_string": "dwrite.dll" }, { "condition_type": "path", "invert": false, "use_regex": false, "search_string": "extension/" } ] }
  ]
}
```

> `auto_tags` here are useful because AC mods **are** extracted (unlike FS), so Limo sees `ui_car.json`/`ui_track.json` and can label cars vs tracks vs apps vs CSP. Validate the exact `condition_type`/`expression` semantics against `src/core/autotag.cpp:27-60` before shipping.

### Optional split (enhancement)

For finer toggling and smaller conflict scopes, offer separate deployers/profiles for **Cars** (`content/cars`), **Tracks** (`content/tracks`), **Apps** (`apps/`), **CSP** (`extension/` + root `dwrite.dll`). Start with the single root deployer; add the split later if users want category toggles.

## 3. The one real gap: inconsistently-rooted archives (importer normalization)

AC archives are rooted inconsistently: some at `content/cars/<car>/…`, some at the bare `<car>/…` folder (with `ui_car.json` at top), and double-nesting (`<car>/<car>/…`) is a common breakage. Limo's `root_level_conditions` can **strip** leading dirs (handles `content/`-rooted and double-nested cases), but it cannot **add** a parent (wrap a bare `<car>/` under `content/cars/`).

- **Phase 1 (ship now):** `root_level_conditions` that recognize archives already rooted at `content`/`apps`/`extension`/`system` and merge as-is; document that bare-car archives need the user to set the root level / place under `content/cars` (the Add-Mod dialog already exposes a root-level spinbox). Validate `level_offset` semantics against `src/ui/rootlevelcondition.cpp:22-54`.
- **Phase 2 (enhancement):** importer auto-normalization that detects the anchor file (`ui_car.json` → `content/cars/`, `ui_track.json` → `content/tracks/`, `apps/(python|lua)/<app>` → `apps/…`, `dwrite.dll`+`extension/` → root) and rewrites the staging layout, flattening double-nesting. This is the "#1 source of user error" the manager should fix; lives in the installer/staging step, not the deployer.

## 4. Sources

OverTake.gg (ex-RaceDepartment), AssettoLand, etc. — **no public download APIs**, session/ad-gated → **manual `.zip`/`.rar` import**. GitHub-hosted CSP config repos (e.g. `ac-custom-shaders-patch/acc-extension-config`) are the only cleanly automatable source. Don't build an in-app downloader for AC initially.

## 5. ACC note

If ACC support is wanted later, it needs a **separate preset** (App ID 805550) targeting the prefix's `Documents/Assetto Corsa Competizione/Customs/` liveries/setups folders — **not** this deployer. Lower value; mention as a stretch.

## 6. Work breakdown

1. Add `steam_app_configs/244210.json` (data) with deployer + auto_tags + phase-1 root_level_conditions. — *Issue #231*
2. (Enhancement) importer normalization for bare-rooted / double-nested archives.
3. (Enhancement) optional Cars/Tracks/Apps/CSP deployer split.
4. (Stretch) ACC preset targeting the prefix `Customs/` folder.
5. Test on a real AC Proton install: deploy a car mod, confirm it shows in-game / Content Manager.

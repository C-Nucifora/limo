I have everything needed. Here is the brief.

---

# Assetto Corsa Mod Ecosystem — Research Brief for Limo Support

## 1. Steam App IDs (verified)

| Game | Steam App ID | `installdir` (steamapps/common) | Notes |
|---|---|---|---|
| **Assetto Corsa** (original) | **244210** | `assettocorsa` | The mod-heavy target. Verified on the Steam store page (title "Assetto Corsa", URL `/app/244210/`). This is where Limo should focus. |
| **Assetto Corsa Competizione** (ACC) | **805550** | `Assetto Corsa Competizione` | Verified on the Steam store page. Much smaller mod scene — mostly liveries/setups, no equivalent content/cars+tracks drop-in modding (see §6). |

The original AC is the priority. ACC is mentioned only for completeness; its modding is comparatively trivial and is *not* the same model.

Sources: [Steam — Assetto Corsa](https://store.steampowered.com/app/244210/), [Steam — ACC](https://store.steampowered.com/app/805550/Assetto_Corsa_Competizione/)

---

## 2. Install location and mod subfolders

Game installs to `$STEAM_INSTALL_PATH$` = `steamapps/common/assettocorsa/`. Mods are merged into this same root tree (there is **no** separate mod directory — modded content lives intermixed with stock Kunos content). The relevant subfolders:

| Subfolder (relative to `assettocorsa/`) | Content type | Per-mod unit |
|---|---|---|
| `content/cars/<car_id>/` | Cars | one folder per car (e.g. `content/cars/ks_ferrari_488_gt3/`) |
| `content/cars/<car_id>/skins/<skin>/` | Liveries / skins | one folder per livery, *inside* an existing car |
| `content/tracks/<track_id>/` | Tracks | one folder per track (with `ui/`, `ai/`, `data/`, `models/`, optional `extension/`; multi-layout tracks nest per-layout subfolders) |
| `apps/python/<app>/` | Python in-game apps (telemetry, HUDs, etc.) | one folder per app |
| `apps/lua/<app>/` | Lua apps (CSP-era, lighter than Python) | one folder per app |
| `content/gui/` | UI graphics/overrides | loose files merged in |
| `content/fonts/`, `content/driver/`, `content/sfx/` | Shared resources some mods touch | loose files merged in |
| `system/` | Engine config, default data (`system/cfg`, `system/data/surface.ini`), VR/Python libs | rarely modded; touched by some tweaks |
| `extension/` | **Custom Shaders Patch (CSP)** and its configs (`extension/config/cars/{common,kunos,mods}/`, `extension/config/tracks/`, Lua SDK, WFX/VAO, fonts) | the CSP framework + per-car/per-track config `.ini` files |
| `dwrite.dll` (in the **root**, next to `acs.exe`) | The CSP loader itself — named `dwrite.dll` so the game auto-injects it on launch | a single DLL at game root |

Note: CSP **user options** (not deployable content) save to the Proton prefix under `…/Documents/Assetto Corsa/cfg/extension/`, i.e. under `$STEAM_PREFIX_PATH$` — Limo should not manage those.

Sources: [ARC modding — root folder](https://arcmite.github.io/arc-website/guides/modding/root-folder.html), [ACAppTutorial](https://github.com/ckendell/ACAppTutorial/blob/master/ACAppTutorial.md), [acc-extension-config README](https://github.com/ac-custom-shaders-patch/acc-extension-config/blob/master/README.md), [SimRacingCockpit — CSP](https://simracingcockpit.gg/what-is-the-custom-shaders-patch-for-assetto-corsa-and-what-does-it-do/)

---

## 3. Mod formats and packaging

- **Packaging:** ZIP or RAR archives (occasionally 7z). There is **no proprietary mod archive format** and no central manifest at the archive level. Two packaging conventions exist in the wild:
  1. **Rooted at `content/`** — the archive contains a `content/cars/<car>/…` (or `content/tracks/…`) tree. Correct install = extract into `assettocorsa/` root and let it merge.
  2. **Rooted at the item folder** — the archive contains just `<car>/…` (with `ui_car.json` at top), and the user must drop it into `content/cars/`.

  This inconsistency is the #1 source of user error and the main thing a manager must normalize (detect whether the tree starts at `content/`, at `cars/`, or at the bare car folder identified by `ui_car.json`/`ui_track.json`).

- **Manifests (per-item, inside the tree, not a mod-level manifest):**
  - `content/cars/<car>/ui/ui_car.json` — name, brand, class, tags, description, specs. (Path is `ui/ui_car.json`; some older layouts put it directly in the car root.)
  - `content/tracks/<track>/ui/ui_track.json` (or `ui/<layout>/ui_track.json` for multi-layout tracks) — track name, length, pitboxes, preview/outline.
  - Car physics ship either as a loose `data/` folder or as an encrypted/packed `data.acd` blob; either way it lives inside the car folder. SFX in `sfx/`, liveries in `skins/<skin>/`.

- **Common failure mode:** double-nesting (`<car>/<car>/…`) — the game won't load it. A deployer/importer should flatten so the folder containing `ui_car.json` lands directly under `content/cars/`.

Sources: [AC Supply — install car mods](https://www.acsupply.cx/guides/install-car-mods), [AC Supply — install track mods](https://www.acsupply.cx/guides/install-track-mods), [actools-uijson](https://github.com/gro-ove/actools-uijson), [Steam guide — manual car install](https://steamcommunity.com/sharedfiles/filedetails/?id=2806884632)

---

## 4. Enabling/disabling mods; Content Manager & CSP; Linux

- **No built-in mod manager.** AC has no mod registry, no load order, no enable/disable toggle in the base game. A mod is simply **present-or-absent on disk**. A car/track is "enabled" iff its folder exists under `content/...` with a valid `ui_*.json`. This is the ideal shape for Limo's hard-link/symlink deploy model: deploying a mod *is* enabling it; un-deploying *is* disabling it.

- **Content Manager (CM):** a third-party `.exe` (the de-facto launcher/UI replacement for AC's stock launcher). It handles drag-and-drop install (auto-detecting the archive's tree depth), car/skin/track browsing, server browser, and — critically — it **installs and updates CSP** and pulls CSP config files. It reads the same on-disk `content/` tree; it does not maintain a separate enabled-list that the game needs. There's a "Content Manager Safe.exe" variant used on Linux.

- **CSP (Custom Shaders Patch):** a graphics/extension framework loaded via `dwrite.dll` at game root, with its payload in `extension/`. Enables new lighting, weather (Sol/Pure), Lua scripting, and many mods *require* it. It is itself "a mod" in deployment terms: a `dwrite.dll` + `extension/` tree dropped into the game root.

- **Running on Linux (Proton/Steam Deck):** Users set the Steam **launch options** to run CM instead of `acs.exe`, e.g.:
  `proton waitforexitandrun ".../assettocorsa/Content Manager Safe.exe"; echo %command%`
  CSP needs a Wine **DLL override for `dwrite`** (native) so the loader injects. CSP/CM live in the same `assettocorsa/` install dir; their config/user data lands in the Proton prefix (`$STEAM_PREFIX_PATH$`). ProtonGE (or a recent Proton) is typically required; some CSP versions are Proton-version-sensitive.

- **Does Limo need to integrate with CM?** **No.** Limo only needs to **deploy files** into `assettocorsa/` (and optionally drop in the CM/CSP files themselves as mods). Because "installed = on disk", CM/CSP will see Limo-deployed content automatically with **zero CM integration, no registry edits, no manifest writes**. Limo's job is purely file placement into the right `content/...`/`apps/...`/`extension/` subtrees. (One caveat: Limo deploys via hard-link/symlink; CM/CSP and the game read those fine, but CM's own "delete from disk" actions should be left to Limo to avoid breaking links.)

Sources: [Steam guide — AC on Linux/Proton/Deck](https://steamcommunity.com/sharedfiles/filedetails/?id=2828364666), [SimRacingCockpit — install CM/CSP/Pure](https://simracingcockpit.gg/install-content-manager-custom-shaders-patch-pure-assetto-corsa/), [OpenMods — getting started](https://openmods.net/guides/getting-started-assetto-corsa), [ProtonDB 244210](https://www.protondb.com/app/244210)

---

## 5. Mod sources

- **OverTake.gg** (formerly RaceDepartment) — the largest curated hub; per-game category pages (`/downloads/categories/assetto-corsa.1/`, split into AC Cars, AC Tracks, apps). Resource manager attaches discussion threads. Downloads are gated behind a logged-in session and per-resource pages; there is **no public/official download API**. Direct programmatic fetch is not officially supported — scraping would require auth/session handling and would be fragile/ToS-sensitive. Realistic Limo approach: user downloads the archive manually, then imports it into Limo.
- **AssettoLand** — large community catalog/aggregator of AC mods (often mirrors/links elsewhere). No API; direct links vary, some ad-gated.
- **Others:** GTPlanet forums, AssettoCorsaMods.net, ACstuff/CSP repos on GitHub (e.g. `ac-custom-shaders-patch/acc-extension-config` — this one *is* git-cloneable directly), and assorted Discords.
- **Feasibility verdict:** treat AC mods as **manually-downloaded local archives** that the user imports. Don't plan an in-app downloader/API for OverTake/AssettoLand initially. The only API-friendly source is GitHub-hosted CSP config repos.

Sources: [OverTake — AC mods category](https://www.overtake.gg/downloads/categories/assetto-corsa.1/), [OverTake — AC Cars](https://www.overtake.gg/downloads/categories/ac-cars.6/), [OverTake — AC Tracks](https://www.overtake.gg/downloads/categories/ac-tracks.8/), [acc-extension-config repo](https://github.com/ac-custom-shaders-patch/acc-extension-config)

---

## 6. Implications for Limo

**Recommended deployer model: a single Case-Matching / drop-in deployer that merges a `content/`-rooted tree into the game root**, with target = `$STEAM_INSTALL_PATH$` (`.../steamapps/common/assettocorsa/`).

- **Why Case Matching:** AC content is folder-keyed and case matters on Linux (the game expects lowercase `content/cars/...`). A Case-Matching Deployer that merges a staged tree into the existing `assettocorsa/` tree is the natural fit — exactly the "drop-in, merge into game root" model. Hard-links are preferred (CM/CSP/game read them transparently).

- **Staging-tree normalization (importer, not deployer):** Because archives are rooted inconsistently (§3), Limo's import step should normalize every mod to a `content/...`-rooted staging layout by detecting the anchor: a folder containing `ui_car.json` → goes under `content/cars/`; `ui_track.json` → `content/tracks/`; an `apps/python|lua/<app>` folder → `apps/...`; a `dwrite.dll` + `extension/` → game root. Flatten double-nesting. This lets one merge deployer handle everything.

- **Single vs. multiple deployers:** A single root deployer covering the whole `assettocorsa/` tree is sufficient and simplest, since all mod classes (cars, tracks, apps, gui, extension) are just different subtrees of the same root and never need independent load orders. **Optional** ergonomic split: separate deployers/profiles for **Cars** (`content/cars`), **Tracks** (`content/tracks`), **Apps** (`apps/`), and **CSP/extension** (`extension/` + root `dwrite.dll`) — purely so users can toggle categories independently and keep conflict scopes small. Recommend starting with one root deployer; offer the split as an enhancement.

- **Conflict handling:** Conflicts occur when two mods write the *same* path. AC has **no load order and no merge resolution** — last-writer-wins on disk, and a partially-overwritten car/track usually breaks. Cases:
  - *Disjoint by design:* each car/track lives in its own uniquely-named folder, so most car/track mods don't collide at all — this is the common, clean case.
  - *Real collisions:* (a) two versions of the *same* car id; (b) skins/liveries dropped into a shared car's `skins/` (here per-skin folders are disjoint, so multiple skin mods coexist cleanly — a good argument for treating skins as their own fine-grained mods); (c) shared resources like `content/gui/`, `content/sfx/`, `system/data/surface.ini`; (d) **CSP `extension/config/...` `.ini` files**, where multiple config packs target the same car/track config — genuine overwrite conflicts.
  - Limo should surface these via its normal per-file conflict UI (priority/winner ordering). Priority *is* the de-facto load order for the few overlapping files; AC needs nothing more.

- **Load-order needs:** Effectively **none at the game level** — AC doesn't read an order. Limo's deploy priority only matters as the tiebreaker for overlapping files (CSP configs, shared GUI/SFX). No LOOT-style sorting, no plugin list. So the **Loot Deployer is not appropriate**; the Case-Matching/drop-in deployer is the correct choice.

- **`auto_tags` / `root_level_conditions` suggestions for the JSON preset:**
  - Auto-tag by detected anchor file: `ui_car.json` → tag "Car"; `ui_track.json` → tag "Track"; `apps/python` or `apps/lua` presence → "App"; `dwrite.dll`/`extension/lua/…` → "CSP/Extension"; a `skins/` payload without a full car → "Skin/Livery".
  - `root_level_conditions`: recognize archives that already start at `content/`, `apps/`, `extension/`, or `system/` as root-level (merge as-is); otherwise wrap the detected item under the correct `content/...` parent.
  - Target paths use `$STEAM_INSTALL_PATH$` for all deployed content; reserve `$STEAM_PREFIX_PATH$` only if Limo ever manages CM/CSP *user config* under the prefix's `Documents/Assetto Corsa/` (recommended: leave that to CM).

- **ACC (805550) note:** does **not** share this model. ACC modding is mainly custom liveries/skins and setups placed under the user's `Documents/Assetto Corsa Competizione/Customs/` (in the Proton prefix, i.e. `$STEAM_PREFIX_PATH$`), not a `content/` drop-in tree in the install dir. If Limo supports ACC at all, it needs a *separate* preset targeting the prefix's `Customs/` folders — not the AC deployer.

Sources: [ARC modding — root folder](https://arcmite.github.io/arc-website/guides/modding/root-folder.html), [AC Supply — car mods](https://www.acsupply.cx/guides/install-car-mods), [AC Supply — track mods](https://www.acsupply.cx/guides/install-track-mods), [acc-extension-config README](https://github.com/ac-custom-shaders-patch/acc-extension-config/blob/master/README.md), [Steam — AC on Linux](https://steamcommunity.com/sharedfiles/filedetails/?id=2828364666)

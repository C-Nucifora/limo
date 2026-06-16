I now have comprehensive, well-sourced information across all topics. Let me compile the final markdown brief.

# Farming Simulator Mod Ecosystem — Research Brief for Limo Support

Scope: FS15, FS17, FS19, FS22, FS25. The mod model is remarkably consistent across all five titles (same GIANTS engine lineage), which is good news for Limo: one deployer pattern covers the whole franchise.

## 1. Steam App IDs (verified)

| Game | Steam App ID | Status |
|------|-------------|--------|
| Farming Simulator 15 | **313160** | Confirmed (seed correct) |
| Farming Simulator 17 | **447020** | Confirmed (seed correct) |
| Farming Simulator 19 | **787860** | Confirmed (seed correct) |
| Farming Simulator 22 | **1248130** | Confirmed (seed correct) |
| Farming Simulator 25 | **2300320** | Confirmed (seed correct) |

All five seed guesses were correct. (Note: many DLCs/packs have their own App IDs, e.g. FS25 season pass 2981100, Vredo Pack 4348790 — these are not the base game and not relevant to the mods folder.)

## 2. Where the mods folder lives

**Windows (canonical):** `Documents\My Games\FarmingSimulator<YEAR>\mods`, where `<YEAR>` is the full 4-digit year, NOT the 2-digit title number:

- FS15 → `FarmingSimulator2015`
- FS17 → `FarmingSimulator2017`
- FS19 → `FarmingSimulator2019`
- FS22 → `FarmingSimulator2022`
- FS25 → `FarmingSimulator2025`

This is a user-data directory, completely separate from the Steam install dir. The install dir (`$STEAM_INSTALL_PATH$`) holds the game binary/base data; mods do NOT go there for single-player.

**Linux / Steam Play (Proton) — the critical part for Limo:** the "Documents" folder is virtualized inside the Proton prefix. The mods folder resolves to:

```
<prefix>/pfx/drive_c/users/steamuser/Documents/My Games/FarmingSimulator<YEAR>/mods
```

With `$STEAM_PREFIX_PATH$` resolving to `steamapps/compatdata/<appid>/pfx`, the full target for FS22 is:

```
$STEAM_PREFIX_PATH$/drive_c/users/steamuser/Documents/My Games/FarmingSimulator2022/mods
```

**Per-version path caveat (verify on the target machine):** community reports show an inconsistency in the virtual Documents segment between older and newer titles/Proton builds:
- FS22/FS25 reports use `.../users/steamuser/Documents/My Games/...`
- An FS19 report used `.../users/steamuser/My Documents/My Games/...`

`My Documents` is historically a Wine symlink/alias to `Documents` inside the prefix, so both can resolve to the same place, but the literal directory created by the game has varied. Recommendation: Limo's deployer should target `.../steamuser/Documents/My Games/FarmingSimulator<YEAR>/mods` and create it if absent; optionally also handle the `My Documents` alias for FS15/17/19 prefixes. The `mods` folder is sometimes not present until the game has been launched once — Limo should create it.

## 3. Mod format and load semantics

- **Format:** A mod is a single `.zip` archive placed directly in the `mods` folder. The archive contains `modDesc.xml` **at its root**, plus assets (i3d models, lua scripts, textures, l10n, etc.). If the zip wraps everything in a top-level subfolder, the game will not read it — `modDesc.xml` must be the first thing the engine sees in the zip root.
- **`modDesc.xml` declares:** the engine `descVersion` (must match the game's mod-API version), `author`, `version`, multilingual `title`/`description`, the mod's icon, and what the mod registers — e.g. `<storeItems>`/vehicles via `vehicle.xml`, maps, scripts (`<extraSourceFiles>`), placeables, fill types, etc. The internal `modName` (effectively the zip filename without `.zip`) is the mod's identity used in savegames.
- **Drop-in, not a merge/overlay:** Mods are NOT extracted or merged into the game tree. Each zip stays a discrete file in `mods/`. The game enumerates every `.zip` in `mods/` and presents them; there is no engine-level load order and no file-overwrite resolution between mods the way Skyrim/loose-files work — each mod is a self-contained package. Conflicts are content-level (two mods adding the same store item / same map), resolved by the user simply not enabling both, not by ordering. This is fundamentally different from the case-matching/overwrite deployers Limo uses for Bethesda games.
- **Filename constraint:** mod zip names should avoid `-` and `.` (other than the `.zip` extension); use `_`. Mods that violate this silently fail to load. Limo must preserve original filenames and not rewrite them with dashes/dots.
- **DLC interaction:** Official DLC is purchased/installed through the in-game "Downloadable Content" store (returns in FS22/FS25) and installed by the game, not dropped into `mods/`. DLC is independent of the user mods folder; mods may *require* a DLC's content but DLC itself is not something Limo needs to deploy. ModHub (the official portal) mods are ordinary `.zip` mods and go in the `mods` folder like any third-party mod.

## 4. How mods are ENABLED (key for "different mods per save")

Two distinct layers — important to model correctly:

1. **Presence in `mods/`** only makes a mod *available*. It does nothing by itself.
2. **Per-savegame selection in the game UI:** When you start a new game or load a save, the game shows a mod-selection screen (after picking the savegame slot / map, before launch) with a **checkbox per available mod**. The set of checked mods is bound to that specific savegame.

**The recording file:** each savegame is a folder `savegame1`, `savegame2`, … under `FarmingSimulator<YEAR>/`, containing `careerSavegame.xml`. At the bottom of `careerSavegame.xml` the game writes the list of mods that save uses, e.g.:

```xml
<mod modName="FS19_EasyDevControls" title="Easy Development Controls"
     version="1.0.0.0" required="false" fileHash="226943e73eee43672d959ff830d4f6f6"/>
```

Each entry records the internal `modName`, title, version, a `required` flag, and a `fileHash`. On load, if a listed mod isn't present in `mods/`, the game warns about missing mods. So: **the mods folder is a shared global pool; which mods a given save actually uses is stored in that save's `careerSavegame.xml`, chosen via in-game checkboxes — not in a global file Limo would write.**

Implication: Limo does NOT need to (and should not try to) write the per-save selection. The user's "different mods for different maps/saves" intent is satisfied at the game level by the checkbox screen, provided the union of mods they might want is present in `mods/`.

## 5. Modpacks

A "modpack" in FS is a community/social convention, not an engine feature: it's just a curated *collection* of individual `.zip` mods (often grouped per-map, per-YouTuber, or per-server). Distribution is typically a single archive containing many mod zips; "installing" the pack means dropping all those `.zip` files into the `mods` folder (unzip the pack, but leave the individual mod zips zipped). There is no manifest the engine reads for a pack — once the zips are in `mods/`, they're indistinguishable from any other mods and are enabled per-save via the checkbox screen.

Because the game reads exactly **one** `mods` folder, the community manages "different packs for different maps" by **swapping the active mods folder**. Third-party tools (FSG Mod Assistant, LS-ModManager) are "mod folder switchers": they maintain multiple collections and swap which one is the active `mods` directory, and many advanced users use **symlinks** so a mod's bytes live once on disk but appear in multiple collection folders. This is precisely the niche Limo can fill natively with hard-links/symlinks.

## 6. Multiplayer / dedicated server

- A dedicated server has its own `mods` folder (in the server's files / control panel); admins upload `.zip` mods there via FTP or web panel and enable them in the server's mod settings, then restart.
- Clients joining a modded server **auto-download the required mods from the server** into a server-specific cache, so a connecting player doesn't manually pre-install the pack. (Some hosts also run separate "mod sync" tools, e.g. FS25-Mod-Sync-Server, for large packs.)
- Same `.zip` + `modDesc.xml` format; same `-`/`.` filename rule. Server modpacks are again just a folder full of mod zips.
- For Limo's purposes the dedicated server is mostly out of scope (it's a separate non-Steam server binary), but the takeaway is that the *format and folder model are identical*, so a Limo deployer aimed at the prefix `mods` folder also fits a locally hosted listen/server setup.

## 7. Implications for Limo

**Deployer model:** A **simple drop-in deployer** is the correct fit. Stage `.zip` mods, then hard-link/copy/symlink them by filename into:

```
$STEAM_PREFIX_PATH$/drive_c/users/steamuser/Documents/My Games/FarmingSimulator<YEAR>/mods
```

No case-matching, no overwrite/conflict resolution, no LOOT-style sorting, and no load order are needed — this is closest to a flat link-files-into-one-target deployer (similar in spirit to the "Reverse Deployer"/plain deployer rather than the Case Matching or Loot deployers). Each title gets its own JSON preset keyed by App ID with a single deployer pointing at its year-specific path.

Concrete preset notes per App ID:
- 313160 → `FarmingSimulator2015`
- 447020 → `FarmingSimulator2017`
- 787860 → `FarmingSimulator2019`
- 1248130 → `FarmingSimulator2022`
- 2300320 → `FarmingSimulator2025`

**Must-haves for correctness:**
- Preserve original zip filenames exactly (the engine maps `modName` → filename and forbids `-`/`.`).
- Create the `mods` directory if the prefix doesn't have it yet (game may not have generated it).
- Treat each `.zip` as an opaque unit — do not unpack; deploy whole archives. So Limo should ingest each mod as "a single root-level `.zip` file" rather than expanding it. (`root_level_conditions` / `auto_tags` could tag by reading `modDesc.xml` from inside the zip — author, title, version — but extraction into the target must NOT happen.)
- For FS15/17/19, also consider the `My Documents` alias variant of the path.

**"Different mods per map" and "multiple packs active at once":**
- These are *not* engine concepts Limo controls via files — the game's per-save checkbox screen (recorded in each save's `careerSavegame.xml`) does the actual per-map selection. Limo's job is to make the *union* of desired mods present in the single `mods` folder; the user then checks the relevant subset per save in-game. This maps naturally onto Limo's profiles: a Limo profile = "the set of mods deployed into `mods/` right now."
- For users who genuinely want isolated, swappable collections (the FSG Mod Assistant workflow), Limo's profile system is a superior native replacement: each Limo load order / profile deploys a different set of links into the same `mods` folder, so "switch to my map-X pack" = "activate Limo profile X." Multiple packs active simultaneously = just deploy all their mods together (they coexist as separate zips; the user enables what they want per save). Limo's hard-link/symlink deployment also solves the disk-duplication problem the community currently hacks with manual symlinks.

### Sources
- FS25 store page (App ID 2300320): https://store.steampowered.com/app/2300320/Farming_Simulator_25/ ; SteamDB: https://steamdb.info/app/2300320/
- FS19 store page (787860): https://store.steampowered.com/app/787860/Farming_Simulator_19/ ; SteamDB: https://steamdb.info/app/787860/
- FS17 (447020) / FS15 (313160) Steam community hubs: https://steamcommunity.com/app/447020 , https://steamcommunity.com/app/313160/discussions/0/343785574520350525/
- FS22 (1248130) hub / mods folder: https://steamcommunity.com/app/1248130/discussions/0/3183486320467998215
- FS22 Windows mods path: https://digistatement.com/farming-simulator-fs-22-how-to-move-the-mod-folder-location/
- FS17 Windows mods path + per-save enable screen: https://steamcommunity.com/app/447020/discussions/0/154644349173551275/
- FS25 Windows mods path + DLC store + don't-unzip: https://www.bisecthosting.com/blog/farming-simulator-25-mod-guide-best-mods-how-to-install , https://fs25.net/how-to-install-mods/
- Proton/Linux mods path (FS22): https://steamcommunity.com/app/1248130/discussions/0/3421062490358375360/
- Proton/Linux mods path (FS19, "My Documents" variant): https://steamcommunity.com/app/787860/discussions/0/1741100729966752338/
- modDesc.xml at zip root / structure: https://forum.giants-software.com/viewtopic.php?t=210387 , https://steamcommunity.com/app/1248130/discussions/0/4423184908204137103/
- careerSavegame.xml mod list (`<mod modName=... fileHash=.../>`): https://steamcommunity.com/app/787860/discussions/0/2941369009261604315/?l=english
- Savegame folders & careerSavegame.xml location: https://www.farming-simulator.org/17/editing.php
- Multiple mod folders / switching / symlinks (FSG Mod Assistant): https://fsgmodding.github.io/FSG_Mod_Assistant/faq.html ; https://www.youtube.com/watch?v=ExXp9DzBzHM ; https://github.com/Kaktushose/LS-ModManager
- Dedicated server mods + client auto-download + filename `_` rule: https://blog.oudel.com/how-to-add-mods-to-fs22-dedicated-server-a-full-guide/ , https://www.bisecthosting.com/clients/index.php?rp=%2Fknowledgebase%2F1241%2FHow-to-install-mods-on-a-Farming-Simulator-22-server.html ; mod sync tool: https://github.com/spliffz/FS25-Mod-Sync-Server

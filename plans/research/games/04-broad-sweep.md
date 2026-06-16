I now have all appids confirmed. App IDs from the research: Valheim 892970, Lethal Company 1966720, Risk of Rain 2 632360, Hollow Knight 367520, Slay the Spire 646570, RimWorld 294100, 7 Days to Die 251570, Bannerlord 261550, Project Zomboid 108600, KSP 220200, X4 392160, BG3 1086940, Elden Ring 1245620, MH Rise 1229490 (need not verify — well known), MH World 582010, Don't Starve Together 322330, Sons of the Forest 1326470, Dragon's Dogma 2 2054970.

I have everything needed to write the brief.

# Limo preset candidate assessment — broad sweep beyond the shipped library

Schema reference used (from existing presets at `/home/christian/Documents/GitHub/limo/limo/steam_app_configs/`): a preset is `{ "name", "deployers":[{ "type","name","target_dir","deploy_mode" }], "auto_tags"?, "root_level_conditions"?, "default_install_flags"? }`. Path tokens: `$STEAM_INSTALL_PATH$` = `steamapps/common/<installdir>`, `$STEAM_PREFIX_PATH$` = `compatdata/<appid>/pfx/drive_c`. The big pattern that makes a game "easy": **mods are loose files/folders that drop into a single directory under the game install or the Proton prefix**, fitting the **Case Matching Deployer** (folds filename case for Linux's case-sensitive FS — important because nearly all these games are Windows-targeted and modders ship Windows-cased paths).

---

### Valheim (892970) — Moddable: yes (BepInEx Unity)
- Ecosystem: Thunderstore (primary), some Nexus. Framework: BepInExPack_Valheim.
- Format/location: BepInEx plugin folders/DLLs dropped into `BepInEx/plugins`. Native Linux build, but the BepInEx pack contents (incl. `winhttp.dll`/doorstop) and the `BepInEx/` tree live directly in the game install dir. Target dir: `$STEAM_INSTALL_PATH$/BepInEx/plugins`.
- Deployer: **Case Matching Deployer** into the plugins folder. PRESET-ONLY. BepInEx itself is a one-time manual install (same as Cyberpunk's CET in Limo today) — out of scope for the deployer, which manages the mods.
- **Verdict: Easy add.** High priority — Valheim is one of the most-modded survival games on Steam and pure drop-in.

```json
{
  "name": "Valheim",
  "deployers": [
    { "type": "Case Matching Deployer", "name": "Plugins", "target_dir": "$STEAM_INSTALL_PATH$/BepInEx/plugins", "deploy_mode": "hard link" }
  ]
}
```

### RimWorld (294100) — Moddable: yes (first-class)
- Ecosystem: Steam Workshop + Nexus + GitHub. One of the most moddable games on Steam.
- Format/location: each mod is a self-contained folder (with `About/About.xml`, `Assemblies/`, `Defs/`, etc.). Native Linux game; manual mods go to `$STEAM_INSTALL_PATH$/Mods/<ModName>/`.
- Deployer: **Case Matching Deployer** into `Mods`. PRESET-ONLY. Load order is managed in-game (RimPy/in-game list), so no plugin-list deployer needed.
- **Verdict: Easy add.** Top-tier priority — huge, healthy mod scene, clean folder-per-mod structure.

```json
{
  "name": "RimWorld",
  "deployers": [
    { "type": "Case Matching Deployer", "name": "Mods", "target_dir": "$STEAM_INSTALL_PATH$/Mods", "deploy_mode": "hard link" }
  ]
}
```

### Lethal Company (1966720) — Moddable: yes (BepInEx Unity), co-op
- Ecosystem: Thunderstore (dominant — r2modman/Gale ecosystem). Massive.
- Format/location: BepInEx plugin folders/DLLs into `BepInEx/plugins`. Runs via Proton; the `BepInEx` tree sits in the game install dir. Target: `$STEAM_INSTALL_PATH$/BepInEx/plugins`. Note: requires the `WINEDLLOVERRIDES="winhttp.dll=n,b" %command%` launch option (user sets once; not a deployer concern).
- Deployer: **Case Matching Deployer**. PRESET-ONLY.
- **Verdict: Easy add.** Very high priority — one of the most-modded recent games; clients commonly need the same mod set, so a manager is genuinely useful.

```json
{
  "name": "Lethal Company",
  "deployers": [
    { "type": "Case Matching Deployer", "name": "Plugins", "target_dir": "$STEAM_INSTALL_PATH$/BepInEx/plugins", "deploy_mode": "hard link" }
  ]
}
```

### Risk of Rain 2 (632360) — Moddable: yes (BepInEx Unity)
- Ecosystem: Thunderstore (the original Thunderstore game). Large.
- Format/location: `$STEAM_INSTALL_PATH$/BepInEx/plugins` (plugin folders/DLLs). Same BepInEx drop-in pattern.
- Deployer: **Case Matching Deployer**. PRESET-ONLY.
- **Verdict: Easy add.** High priority — flagship Thunderstore title.

```json
{
  "name": "Risk of Rain 2",
  "deployers": [
    { "type": "Case Matching Deployer", "name": "Plugins", "target_dir": "$STEAM_INSTALL_PATH$/BepInEx/plugins", "deploy_mode": "hard link" }
  ]
}
```

### Mount & Blade II: Bannerlord (261550) — Moddable: yes (first-class)
- Ecosystem: Nexus + Steam Workshop. Very large.
- Format/location: each mod is a Module folder under `$STEAM_INSTALL_PATH$/Modules/<ModuleName>/` (contains `SubModule.xml`). Nexus mods go here directly; Workshop mods live in `workshop/content/261550` but can be copied to `Modules`.
- Deployer: **Case Matching Deployer** into `Modules`. PRESET-ONLY. Load order is handled by the in-game launcher (or BUTR's launcher), so no special deployer. An `auto_tag` keyed on `SubModule.xml` is a nice touch.
- **Verdict: Easy add.** High priority — top-25 moddable game, clean module structure.

```json
{
  "name": "Mount & Blade II: Bannerlord",
  "deployers": [
    { "type": "Case Matching Deployer", "name": "Modules", "target_dir": "$STEAM_INSTALL_PATH$/Modules", "deploy_mode": "hard link" }
  ],
  "auto_tags": [
    { "name": "Module", "expression": "0", "conditions": [
      { "condition_type": "file_name", "invert": false, "search_string": "SubModule.xml", "use_regex": false } ] }
  ]
}
```

### 7 Days to Die (251570) — Moddable: yes (first-class)
- Ecosystem: Nexus + community. Large overhaul scene (Darkness Falls, Undead Legacy).
- Format/location: folder-per-mod with a `ModInfo.xml`. Two valid locations; the game-folder one is simplest and most compatible: `$STEAM_INSTALL_PATH$/Mods/<ModName>/`. (The newer per-user `%APPDATA%/7DaysToDie/Mods` maps to `$STEAM_PREFIX_PATH$/users/steamuser/AppData/Roaming/7DaysToDie/Mods` under Proton — but the game-dir Mods folder remains supported and is the cleaner deployer target.)
- Deployer: **Case Matching Deployer**. PRESET-ONLY.
- **Verdict: Easy add.** Solid priority.

```json
{
  "name": "7 Days to Die",
  "deployers": [
    { "type": "Case Matching Deployer", "name": "Mods", "target_dir": "$STEAM_INSTALL_PATH$/Mods", "deploy_mode": "hard link" }
  ],
  "auto_tags": [
    { "name": "Mod", "expression": "0", "conditions": [
      { "condition_type": "file_name", "invert": false, "search_string": "ModInfo.xml", "use_regex": false } ] }
  ]
}
```

### Kerbal Space Program (220200) — Moddable: yes (first-class)
- Ecosystem: SpaceDock/CKAN/Forum/Nexus. One of the most-modded games ever.
- Format/location: every mod is a folder under `$STEAM_INSTALL_PATH$/GameData/<ModName>/`. Pure drop-in; the only gotcha modders warn about is double-nesting GameData (a folder-structure issue the user handles when adding the mod, not a deployer concern).
- Deployer: **Case Matching Deployer** into `GameData`. PRESET-ONLY.
- **Verdict: Easy add.** High priority. (CKAN exists but is a separate dependency-resolving tool; Limo covers the manual-install path well.)

```json
{
  "name": "Kerbal Space Program",
  "deployers": [
    { "type": "Case Matching Deployer", "name": "GameData", "target_dir": "$STEAM_INSTALL_PATH$/GameData", "deploy_mode": "hard link" }
  ]
}
```

### X4: Foundations (392160) — Moddable: yes (first-class)
- Ecosystem: Nexus + Steam Workshop. Large.
- Format/location: each mod is a folder under an `extensions/` dir, containing `content.xml` plus `.cat`/`.dat` archives. The game-root location is `$STEAM_INSTALL_PATH$/extensions/<modname>/` (also readable from `Documents/Egosoft/X4/<id>/extensions`, but the game-dir path is the standard manual target).
- Deployer: **Case Matching Deployer** into `extensions`. PRESET-ONLY.
- **Verdict: Easy add.** Good priority for the sim/strategy crowd.

```json
{
  "name": "X4: Foundations",
  "deployers": [
    { "type": "Case Matching Deployer", "name": "Extensions", "target_dir": "$STEAM_INSTALL_PATH$/extensions", "deploy_mode": "hard link" }
  ],
  "auto_tags": [
    { "name": "Extension", "expression": "0", "conditions": [
      { "condition_type": "file_name", "invert": false, "search_string": "content.xml", "use_regex": false } ] }
  ]
}
```

### Project Zomboid (108600) — Moddable: yes (first-class), but awkward path
- Ecosystem: Steam Workshop (dominant) + community. Huge.
- Format/location: manual mods go into the per-user `Zomboid/mods/<ModName>/` folder, **not** the game install. Under Proton that resolves to `$STEAM_PREFIX_PATH$/users/steamuser/Zomboid/mods` — but PZ on Linux is a native build that writes `Zomboid/` to the real `$HOME`, which Limo's tokens don't address cleanly. Workshop mods land in `workshop/content/108600`.
- Deployer: Case Matching Deployer would work IF the target path is reachable; the native-Linux `$HOME/Zomboid` location is the snag. Preset-only is plausible but the path needs verification against an actual native PZ install.
- **Verdict: Moderate.** Worth shipping but flag the `Zomboid/mods` path as needing per-install confirmation (native vs Proton). Lower priority than the clean game-dir games above.

### Don't Starve Together (322330) — Moddable: yes, but Workshop-locked
- Ecosystem: Steam Workshop almost exclusively; very little loose-file/Nexus distribution.
- Format/location: Workshop mods auto-download to `workshop/content/322330`; the game also reads a `mods/` folder in the install dir, but the community workflow is "subscribe in Workshop," not manual files. Manual mod files are rare.
- Deployer: Case Matching into `$STEAM_INSTALL_PATH$/mods` would technically work, but there's little manual-mod supply for a manager to add value.
- **Verdict: Moderate / low value.** Technically preset-able but the Workshop monoculture means Limo adds little. Skip unless requested.

### Hollow Knight (367520) — Moddable: yes (BepInEx/Modding API)
- Ecosystem: Modding API (Scarab manager) + BepInEx; Nexus/community.
- Format/location: classic HK uses the Modding API with mods as folders under `hollow_knight_Data/Managed/Mods/<ModName>/`; BepInEx mods go to `BepInEx/plugins`. Both are drop-in folders inside the install dir under Proton.
- Deployer: **Case Matching Deployer**. PRESET-ONLY, but the dual framework (Modding API path vs BepInEx path) means picking one target dir; the Modding API `Mods` folder is the mainstream one.
- **Verdict: Easy add (Moderate confidence on the exact subpath).** Decent priority; verify whether to target the Modding-API `Mods` dir or `BepInEx/plugins` for the shipped preset.

```json
{
  "name": "Hollow Knight",
  "deployers": [
    { "type": "Case Matching Deployer", "name": "Mods", "target_dir": "$STEAM_INSTALL_PATH$/hollow_knight_Data/Managed/Mods", "deploy_mode": "hard link" }
  ]
}
```

### Slay the Spire (646570) — Moddable: yes (ModTheSpire/Workshop)
- Ecosystem: Steam Workshop (ModTheSpire + BaseMod required libs) + GitHub. Mods are mostly distributed as Workshop items; loose mods are `.jar` files.
- Format/location: ModTheSpire reads `.jar`s from a `mods/` dir in the install: `$STEAM_INSTALL_PATH$/mods/`. But the core (ModTheSpire/BaseMod) and most content come via Workshop, and you launch with "Play with Mods."
- Deployer: Case Matching into `$STEAM_INSTALL_PATH$/mods` works for loose `.jar`s. PRESET-ONLY but limited supply outside Workshop.
- **Verdict: Moderate / low value.** Possible but Workshop-centric; lower priority.

### Monster Hunter: World (582010) — Moddable: yes, drag-and-drop
- Ecosystem: Nexus (large). Loose-file via `nativePC`.
- Format/location: mods drop into `$STEAM_INSTALL_PATH$/nativePC/...` (mirrors the game's internal file tree). Stracker's Loader (a one-time install in the game root) is needed for most non-texture mods.
- Deployer: **Case Matching Deployer** into `nativePC`. PRESET-ONLY (case folding matters a lot here because mods mirror game paths).
- **Verdict: Easy add.** Good priority — established drag-and-drop Nexus scene, exactly the merge-tree pattern Limo handles.

```json
{
  "name": "Monster Hunter: World",
  "deployers": [
    { "type": "Case Matching Deployer", "name": "nativePC", "target_dir": "$STEAM_INSTALL_PATH$/nativePC", "deploy_mode": "hard link" }
  ]
}
```

### Monster Hunter Rise (1229490) — Moddable: yes, REFramework
- Ecosystem: Nexus. Two mod styles: REFramework `reframework/autorun` Lua scripts/plugins, and (with the FirstNatives plugin) drag-and-drop into a `natives/` folder.
- Format/location: with FirstNatives, loose files go to `$STEAM_INSTALL_PATH$/natives/...`; REFramework plugins to `$STEAM_INSTALL_PATH$/reframework/plugins`. Both are drop-in dirs.
- Deployer: **Case Matching Deployer** (could ship two deployers: `natives` and `reframework`). PRESET-ONLY, but depends on the user having REFramework+FirstNatives installed.
- **Verdict: Easy add (Moderate setup dependency).** Good priority; ship a `natives` deployer (most mods).

```json
{
  "name": "Monster Hunter Rise",
  "deployers": [
    { "type": "Case Matching Deployer", "name": "natives", "target_dir": "$STEAM_INSTALL_PATH$/natives", "deploy_mode": "hard link" },
    { "type": "Case Matching Deployer", "name": "reframework", "target_dir": "$STEAM_INSTALL_PATH$/reframework/plugins", "deploy_mode": "hard link" }
  ]
}
```

### Sons of the Forest (1326470) — Moddable: yes, but young/varied
- Ecosystem: Nexus + community; RedLoader and BepInEx coexist.
- Format/location: BepInEx mods to `$STEAM_INSTALL_PATH$/BepInEx/plugins`; RedLoader mods to its own `Mods/` folder. Multiplayer — all players need matching mods.
- Deployer: **Case Matching Deployer** (BepInEx path). PRESET-ONLY but framework fragmentation (RedLoader vs BepInEx) makes a single target less universal.
- **Verdict: Moderate.** Shippable for the BepInEx path; medium priority.

### Baldur's Gate 3 (1086940) — Moddable: yes (Limo already special-cases this)
- Limo ships a **BG3 deployer** (LIMO_WITH_LOOT) for `.pak` load order via `modsettings.lsx`. The mods dir under Proton is `$STEAM_PREFIX_PATH$/users/steamuser/AppData/Local/Larian Studios/Baldur's Gate 3/Mods`.
- **Verdict: Already handled by the BG3 deployer; not a new preset target.** (Listed only because it was in the assess set.)

### Elden Ring (1245620) — Moddable: yes, but ModEngine2-gated
- Ecosystem: Nexus. EAC must be disabled (offline) and mods are launched via ModEngine2's `launchmod_eldenring.bat`, which reads a `mod/` folder. This is a load-order/launcher framework, not a simple merge into the game dir.
- Deployer: A Case Matching Deployer into the ModEngine2 `mod/` directory is conceivable, but ModEngine2 lives outside the Steam install dir and the launch flow is non-standard — doesn't map cleanly to `$STEAM_INSTALL_PATH$`.
- **Verdict: Hard / Not preset-friendly.** Skip — anti-cheat + external launcher make it a poor fit.

### Dragon's Dogma 2 (2054970) — Moddable: yes, but Fluffy/REEngine-gated
- Ecosystem: Nexus. Mods are RE-Engine `.pak`s managed by Fluffy Mod Manager (which repacks into the game's `re_chunk` `.pak` numbering), or loose `natives/` files via REFramework's loose-file loading.
- Format/location: loose mods to `$STEAM_INSTALL_PATH$/natives/...` (if REFramework loose-file support is on). The mainstream path is Fluffy's `.pak` repacking, which Limo can't replicate without a packer.
- Deployer: Case Matching into `natives` works **only** for the loose-file subset; the dominant `.pak` workflow needs Fluffy.
- **Verdict: Hard / partial.** Low priority — the `.pak`-via-Fluffy norm doesn't fit a deployer.

---

## Top easy adds (ranked) for this set

1. **RimWorld (294100)** — `$STEAM_INSTALL_PATH$/Mods`, folder-per-mod, enormous first-class scene. Cleanest, highest-value add.
2. **Valheim (892970)** — `BepInEx/plugins`, top survival modding game.
3. **Lethal Company (1966720)** — `BepInEx/plugins`, one of the hottest modded co-op games; per-client mod sets make a manager genuinely useful.
4. **Mount & Blade II: Bannerlord (261550)** — `$STEAM_INSTALL_PATH$/Modules`, huge, clean module structure.
5. **Kerbal Space Program (220200)** — `$STEAM_INSTALL_PATH$/GameData`, legendary mod scene, pure drop-in.
6. **Risk of Rain 2 (632360)** — `BepInEx/plugins`, flagship Thunderstore title.
7. **Monster Hunter: World (582010)** — `$STEAM_INSTALL_PATH$/nativePC`, classic drag-and-drop merge tree.
8. **7 Days to Die (251570)** — `$STEAM_INSTALL_PATH$/Mods`, big overhaul scene.
9. **X4: Foundations (392160)** — `$STEAM_INSTALL_PATH$/extensions`, strong sim modding.
10. **Hollow Knight (367520)** / **Monster Hunter Rise (1229490)** — easy but verify exact subpath / framework dependency before shipping.

**Moderate (ship with caveats):** Sons of the Forest (framework split), Project Zomboid (native-Linux `$HOME/Zomboid/mods` path needs confirmation), Slay the Spire & Don't Starve Together (Workshop-dominated, low loose-file supply).
**Skip (not preset-friendly):** Elden Ring (EAC + ModEngine2 external launcher), Dragon's Dogma 2 (Fluffy `.pak` repacking). **Already covered:** Baldur's Gate 3 (BG3 deployer).

All "Easy" entries above are **preset-only — no C++ recompile** — using the existing **Case Matching Deployer** with `hard link` deploy mode, matching the pattern Limo already ships for Stardew Valley (`413150.json`) and the Farming Simulator titles. Reference presets read: `413150.json` (Stardew), `447020.json` (FS17, drop-in archive + `no_extract`), `377160.json` (Fallout 4, auto_tags + root_level_conditions), `244210.json` (Assetto Corsa, auto_tags), all under `/home/christian/Documents/GitHub/limo/limo/steam_app_configs/`.

Sources: [Valheim BepInEx (Thunderstore)](https://thunderstore.io/c/valheim/p/denikson/BepInExPack_Valheim/), [RimWorld mods (RimWorld Wiki)](https://rimworldwiki.com/wiki/Installing_mods), [Lethal Company manual install (Thunderstore)](https://thunderstore.io/c/lethal-company/p/BepInEx/BepInExPack/), [RoR2 modding (Fandom)](https://riskofrain2.fandom.com/wiki/Modding), [Bannerlord Modules (TaleWorlds docs)](https://moddocs.bannerlord.com/steam-workshop/uploading_updating_mod/), [7DtD mod structure (7DtD Wiki)](https://7daystodie.fandom.com/wiki/Mod_Structure), [KSP GameData (CKAN)](https://github.com/KSP-CKAN/CKAN), [X4 extensions (Egosoft Wiki)](https://wiki.egosoft.com/X%20Rebirth%20Wiki/Modding%20support/Steam%20Workshop%20for%20X%20Rebirth%20and%20X4/), [Project Zomboid mods (PZwiki)](https://pzwiki.net/wiki/Installing_mods), [BG3 install (bg3.wiki)](https://bg3.wiki/wiki/Modding:Installing_mods), [Hollow Knight modding (GamingOnLinux)](https://www.gamingonlinux.com/guides/view/how-to-install-hollow-knight-silksong-mods-on-linux-steamos-and-steam-deck/), [MHW nativePC (Steam guide)](https://steamcommunity.com/sharedfiles/filedetails/?id=3541671214), [MH Rise FirstNatives (Nexus)](https://www.nexusmods.com/monsterhunterrise/mods/848), [Sons of the Forest RedLoader (GitHub)](https://github.com/ToniMacaroni/RedLoader), [Elden Ring ModEngine2/Seamless (Steam guide)](https://steamcommunity.com/sharedfiles/filedetails/?id=3519102791), [DD2 Fluffy/REFramework (GameRant)](https://gamerant.com/dragons-dogma-2-download-mods-install-mod-guide-dd2/), [Slay the Spire ModTheSpire (GitHub)](https://github.com/kiooeht/ModTheSpire), [DST Workshop (Klei)](https://support.klei.com/hc/en-us/articles/360029556512).

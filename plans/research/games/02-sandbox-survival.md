I now have all the facts I need. tModLoader is verified by Steam DB as native Linux (it has a native Linux build), but since Limo uses Proton prefix tokens, and tModLoader is one of the few games with a true Linux-native build, mods may land in `~/.local/share/Terraria/tModLoader/Mods` natively rather than the prefix. This is a nuance I'll flag. Let me write the final brief.

# Limo game-addition assessment — Sandbox / builder / survival / strategy set

Schema confirmed against shipped presets (e.g. `377160.json`, `1248130.json`, `2300320.json`): a preset is `name` + `deployers[]` (each with `type`, `name`, `target_dir`, `deploy_mode`), optional `auto_tags[]`, `root_level_conditions[]`, and `default_install_flags` (e.g. `["no_extract"]` for whole-archive mods). Path tokens: `$STEAM_INSTALL_PATH$` = `steamapps/common/<installdir>`; `$STEAM_PREFIX_PATH$` = Proton `compatdata/<appid>/pfx/drive_c`.

---

### Factorio (427520) — Moddable: YES (first-class, official mod portal)
- **Ecosystem:** Official [mods.factorio.com](https://mods.factorio.com) portal + in-game mod manager. Mods are distributed and consumed as **whole `.zip` archives** (the game reads the zip directly; never unzipped).
- **Format/location:** zip per mod in the mods folder. On Steam+Proton, Factorio writes user data into the Proton prefix's `%AppData%`: `$STEAM_PREFIX_PATH$/users/steamuser/AppData/Roaming/Factorio/mods`. (Native-Linux installs use `~/.factorio/mods`, but Limo manages the Proton prefix.)
- **Deployer fit:** Case Matching Deployer + `no_extract` flag — identical pattern to the shipped Farming Simulator presets. **PRESET-ONLY.**
- **Verdict: EASY ADD.** Top-tier candidate — huge mod ecosystem, dead-simple drop-in-zip model. Highest priority in this set.

```json
{
  "name": "Factorio",
  "deployers": [
    {
      "type": "Case Matching Deployer",
      "name": "Mods",
      "target_dir": "$STEAM_PREFIX_PATH$/users/steamuser/AppData/Roaming/Factorio/mods",
      "deploy_mode": "hard link"
    }
  ],
  "default_install_flags": [ "no_extract" ]
}
```
Sources: [Factorio Wiki – Application directory](https://wiki.factorio.com/Application_directory), [Nodecraft install guide](https://nodecraft.com/support/games/factorio/downloading-and-installing-mods-on-factorio).

---

### Cities: Skylines (255710) — Moddable: YES
- **Ecosystem:** [Steam Workshop](https://steamcommunity.com/app/255710/workshop/) (primary) + non-Workshop mods from [Skymods/smods.ru](https://smods.ru/how-to-install-mods-for-cities-skylines). Unity game; mods are loose folders (each mod = a subfolder, often containing a managed DLL + assets).
- **Format/location:** loose mod folders under `Addons/Mods`. Cities: Skylines has a **native Linux build**, so a native install reads `~/.local/share/Colossal Order/Cities_Skylines/Addons/Mods`. Under Proton it would be `$STEAM_PREFIX_PATH$/users/steamuser/AppData/Local/Colossal Order/Cities_Skylines/Addons/Mods`. Since Limo targets the Proton prefix, use the prefix path; flag that native-Linux users need the XDG path.
- **Deployer fit:** Case Matching Deployer, loose folders merged into `Addons/Mods`. **PRESET-ONLY**, but the native-vs-Proton path split is the only wrinkle.
- **Verdict: EASY ADD (Moderate caveat).** Large Workshop catalog. Priority: high, after Factorio.

```json
{
  "name": "Cities: Skylines",
  "deployers": [
    {
      "type": "Case Matching Deployer",
      "name": "Mods",
      "target_dir": "$STEAM_PREFIX_PATH$/users/steamuser/AppData/Local/Colossal Order/Cities_Skylines/Addons/Mods",
      "deploy_mode": "hard link"
    }
  ]
}
```
Sources: [CS Modding docs – Folder Structure](https://skylines-modding-docs.readthedocs.io/en/latest/general/Folder-Structure.html), [Paradox Wiki – Folder Structure](https://skylines.paradoxwikis.com/Folder_Structure).

---

### Space Engineers (244850) — Moddable: YES
- **Ecosystem:** [Steam Workshop](https://steamcommunity.com/app/244850/workshop/) (dominant). For manual/offline use, mods go in a local `Mods` folder; each mod = a folder.
- **Format/location:** local mods live in the prefix `%AppData%/Roaming`: `$STEAM_PREFIX_PATH$/users/steamuser/AppData/Roaming/SpaceEngineers/Mods`. (Workshop copies live in `steamapps/workshop/content/244850`, which Limo doesn't manage.)
- **Deployer fit:** Case Matching Deployer, loose folders. **PRESET-ONLY.**
- **Verdict: EASY ADD.** Solid mod scene; clean AppData target. Priority: high.

```json
{
  "name": "Space Engineers",
  "deployers": [
    {
      "type": "Case Matching Deployer",
      "name": "Mods",
      "target_dir": "$STEAM_PREFIX_PATH$/users/steamuser/AppData/Roaming/SpaceEngineers/Mods",
      "deploy_mode": "hard link"
    }
  ]
}
```
Sources: [Space Engineers Wiki – Creating/Uploading Mods](https://spaceengineers.wiki.gg/wiki/Modding/Tutorials/Creating_And_Uploading_Mods), [Steam discussion – workshop mod storage](https://steamcommunity.com/app/244850/discussions/0/135509724376355923/).

---

### Timberborn (1062090) — Moddable: YES (two distinct mod systems)
- **Ecosystem:** [Thunderstore](https://timberborn.thunderstore.io/) (BepInEx DLL mods) + [mod.io](https://mod.io/g/timberborn) / Steam Workshop (native content mods). Two install targets:
  - Native/content mods → `Documents/Timberborn/Mods` → prefix path `$STEAM_PREFIX_PATH$/users/steamuser/Documents/Timberborn/Mods`.
  - BepInEx code mods → `$STEAM_INSTALL_PATH$/BepInEx/plugins` (after BepInExPack is unpacked into the game dir).
- **Format/location:** mod folders (native) and DLLs (BepInEx).
- **Deployer fit:** Two Case Matching Deployers (one per target) — exactly the multi-deployer shape used by the Fallout/Skyrim presets. **PRESET-ONLY.**
- **Verdict: EASY ADD.** Active Thunderstore community. Priority: medium-high.

```json
{
  "name": "Timberborn",
  "deployers": [
    {
      "type": "Case Matching Deployer",
      "name": "Mods",
      "target_dir": "$STEAM_PREFIX_PATH$/users/steamuser/Documents/Timberborn/Mods",
      "deploy_mode": "hard link"
    },
    {
      "type": "Case Matching Deployer",
      "name": "BepInEx Plugins",
      "target_dir": "$STEAM_INSTALL_PATH$/BepInEx/plugins",
      "deploy_mode": "hard link"
    }
  ]
}
```
Sources: [BepInExPack Timberborn (Thunderstore)](https://timberborn.thunderstore.io/package/BepInEx/BepInExPack_Timberborn/), [mod.io install guide](https://mod.io/g/timberborn/r/how-to-install-mods).

---

### Colony Survival (366090) — Moddable: YES
- **Ecosystem:** [Steam Workshop](https://steamcommunity.com/app/366090/workshop/) + manual mods (GitHub etc.). Mods are loose folders dropped into the game's own `gamedata/mods` folder *inside the install dir*.
- **Format/location:** `$STEAM_INSTALL_PATH$/gamedata/mods` (each mod = a subfolder). This is in the game install tree, not the prefix.
- **Deployer fit:** Case Matching Deployer into the install dir — same simplicity as the Stardew preset. **PRESET-ONLY.**
- **Verdict: EASY ADD.** Smaller ecosystem but trivially clean install model. Priority: medium.

```json
{
  "name": "Colony Survival",
  "deployers": [
    {
      "type": "Case Matching Deployer",
      "name": "Mods",
      "target_dir": "$STEAM_INSTALL_PATH$/gamedata/mods",
      "deploy_mode": "hard link"
    }
  ]
}
```
Sources: [Colony Survival Wiki – Mods](https://colonysurvival.fandom.com/wiki/Mods), [Steam guide – install mods](https://steamcommunity.com/app/366090/discussions/0/1727575977582167498/).

---

### No Man's Sky (275850) — Moddable: YES (with version churn)
- **Ecosystem:** [Nexus Mods](https://www.nexusmods.com/nomanssky). Mods are `.pak` files; you also must delete `DISABLEMODS.TXT`.
- **Format/location:** `$STEAM_INSTALL_PATH$/GAMEDATA/MODS` (drop `.pak` files there; create `MODS` if absent). In the game install tree.
- **Deployer fit:** Simple/Case Matching Deployer into `GAMEDATA/MODS`. **PRESET-ONLY** for file deployment. Caveat: as of game v5.5 the team changed `.pak` handling, so some old mods need repacking — that's a *mod-author* concern, not a Limo concern; the deploy target is unchanged. The `DISABLEMODS.TXT` deletion is a one-time manual step Limo doesn't automate.
- **Verdict: EASY ADD (Moderate caveat).** Priority: medium — note the manual DISABLEMODS step.

```json
{
  "name": "No Man's Sky",
  "deployers": [
    {
      "type": "Case Matching Deployer",
      "name": "Mods",
      "target_dir": "$STEAM_INSTALL_PATH$/GAMEDATA/MODS",
      "deploy_mode": "hard link"
    }
  ]
}
```
Sources: [No Man's Sky Wiki – Mods](https://nomanssky.fandom.com/wiki/Mods), [STEP NMS guide](https://stepmodifications.org/wiki/Step_No_Man's_Sky_Guide).

---

### Palworld (1623730) — Moddable: LIMITED (single-player/dedicated; works via Proton)
- **Ecosystem:** [Nexus Mods](https://www.nexusmods.com/palworld). Two mod classes: **pak mods** (drop-in `.pak`, no tooling) and **UE4SS Lua/C++ mods** (need RE-UE4SS injected). UE4SS now has Proton/Linux support. Anti-cheat is not a blocker for single-player/private servers, though Workshop-delivered UE4SS reportedly has issues on Linux.
- **Format/location:**
  - pak mods → `$STEAM_INSTALL_PATH$/Pal/Content/Paks/~mods` (clean drop-in).
  - UE4SS Lua mods → `$STEAM_INSTALL_PATH$/Pal/Binaries/Win64/ue4ss/Mods`.
- **Deployer fit:** Case Matching Deployer(s) into the install tree. pak mods = clean **PRESET-ONLY**. UE4SS mods deploy fine too, but UE4SS itself is a one-time manual prerequisite Limo doesn't install.
- **Verdict: MODERATE.** pak-only support is easy; full UE4SS workflow needs out-of-band setup. Priority: medium — ship a pak-focused preset.

```json
{
  "name": "Palworld",
  "deployers": [
    {
      "type": "Case Matching Deployer",
      "name": "Pak Mods",
      "target_dir": "$STEAM_INSTALL_PATH$/Pal/Content/Paks/~mods",
      "deploy_mode": "hard link"
    },
    {
      "type": "Case Matching Deployer",
      "name": "UE4SS Mods",
      "target_dir": "$STEAM_INSTALL_PATH$/Pal/Binaries/Win64/ue4ss/Mods",
      "deploy_mode": "hard link"
    }
  ]
}
```
Sources: [GHOSTCAP – UE4SS/Pak/Lua mod guide](https://www.ghostcap.com/guides/games/palworld/mod-installation), [RE-UE4SS Linux support (Nexus)](https://www.nexusmods.com/palworld/mods/3405).

---

### Terraria (105600) — Moddable: via tModLoader, not the base game
- **Ecosystem:** Base Terraria (105600) has **no native loose-file mod folder**; modding is done by launching the separate **tModLoader** app (1281930). Texture-pack/config tweaks exist but the real mod scene lives entirely under tModLoader.
- **Verdict: best handled as tModLoader (below), not as a 105600 preset.** Adding a 105600 preset is low value. Priority: skip in favor of 1281930.
Source: [Official Terraria Wiki – tModLoader](https://terraria.wiki.gg/wiki/TModLoader).

---

### tModLoader (1281930) — Moddable: YES (this is the modding platform)
- **Ecosystem:** [Steam Workshop](https://steamcommunity.com/workshop/browse/?appid=1281930) + in-game Mod Browser. Mods are single `.tmod` files.
- **Format/location:** `.tmod` files in `…/Terraria/tModLoader/Mods`. **Nuance:** tModLoader ships a true **native Linux build**, so a native install writes to `~/.local/share/Terraria/tModLoader/Mods` (XDG), *not* the Proton prefix. If run under Proton, it's `$STEAM_PREFIX_PATH$/users/steamuser/Documents/My Games/Terraria/tModLoader/Mods`. Because most Linux users run tModLoader natively, the `$STEAM_PREFIX_PATH$` token may point at the wrong place — this is the one preset where the path token model is a poor fit.
- **Deployer fit:** Case Matching Deployer + `no_extract` (each mod is a single `.tmod`). Mechanically **PRESET-ONLY**, but the native-Linux data path means the standard prefix token doesn't reliably resolve.
- **Verdict: MODERATE.** Easy mechanics, but native-vs-Proton path ambiguity needs a doc note or per-user override. Priority: medium (provide the Proton-prefix preset, document the native path).

```json
{
  "name": "tModLoader",
  "deployers": [
    {
      "type": "Case Matching Deployer",
      "name": "Mods",
      "target_dir": "$STEAM_PREFIX_PATH$/users/steamuser/Documents/My Games/Terraria/tModLoader/Mods",
      "deploy_mode": "hard link"
    }
  ],
  "default_install_flags": [ "no_extract" ]
}
```
Sources: [Terraria Wiki – tModLoader paths](https://terraria.fandom.com/wiki/TModLoader), [SteamDB 1281930](https://steamdb.info/app/1281930/).

---

### Sid Meier's Civilization VI (289070) — Moddable: YES
- **Ecosystem:** [Steam Workshop](https://steamcommunity.com/app/289070/workshop/) + non-Workshop mods ([CivFanatics](https://forums.civfanatics.com/), Skymods). Each mod = a subfolder (often with a `.modinfo` + SQL/XML/Lua + assets).
- **Format/location:** `Documents/My Games/Sid Meier's Civilization VI/Mods` → prefix path `$STEAM_PREFIX_PATH$/users/steamuser/Documents/My Games/Sid Meier's Civilization VI/Mods`. (Civ VI has a native Linux/Mac build; native installs use `~/.local/share/aspyr-media/Sid Meier's Civilization VI/Mods`. Limo's prefix token targets the Proton case.)
- **Deployer fit:** Case Matching Deployer, loose folders. **PRESET-ONLY.** Useful `auto_tag` for `.modinfo` files to surface load-relevant mods.
- **Verdict: EASY ADD (native-path caveat).** Large mod catalog. Priority: medium-high.

```json
{
  "name": "Sid Meier's Civilization VI",
  "deployers": [
    {
      "type": "Case Matching Deployer",
      "name": "Mods",
      "target_dir": "$STEAM_PREFIX_PATH$/users/steamuser/Documents/My Games/Sid Meier's Civilization VI/Mods",
      "deploy_mode": "hard link"
    }
  ],
  "auto_tags": [
    {
      "name": "ModInfo",
      "expression": "0",
      "conditions": [
        { "condition_type": "file_name", "invert": false, "search_string": ".*\\.modinfo$", "use_regex": true }
      ]
    }
  ]
}
```
Sources: [Civ Wiki – Modding (Civ6)](https://civilization.fandom.com/wiki/Modding_(Civ6)), [Skymods – how to install Civ VI mods](https://catalogue.smods.ru/how-to-install-mods-for-sid-meiers-civilization-vi).

---

### House Flipper (613100) — Moddable: YES but Workshop-centric (limited for Limo)
- **Ecosystem:** [Steam Workshop only](https://steamcommunity.com/app/613100/workshop/) — the devs state Workshop is "the only official place." There is **no documented loose-file mods folder for the original House Flipper (613100)**; the manual-folder paths that exist apply to *House Flipper 2* (1190970, mod.io) and the *Remastered Collection* (different appid, `Frozen Way/House Flipper Remastered Collection/Mods`), not to 613100. GOG copies have a manual workaround, but the Steam build expects Workshop.
- **Format/location:** Workshop content lands in `steamapps/workshop/content/613100`, which Limo does not manage; no first-class user mod folder to target.
- **Deployer fit:** No reliable loose-file target on the Steam build → no clean deployer mapping. A Reverse/Workshop approach doesn't apply here.
- **Verdict: NOT A GOOD FIT (skip).** Workshop-only with no documented manual mod directory for this appid. Lowest priority; revisit only if a confirmed loose-file path surfaces.
Sources: [gamepressure – HF Workshop/modding tools](https://www.gamepressure.com/newsroom/house-flipper-with-steam-workshop-and-modding-tools/z92a5a), [Steam discussion – GOG manual install](https://steamcommunity.com/app/613100/discussions/0/3414304680789843615/).

---

## Top easy adds (ranked) for this set
1. **Factorio (427520)** — official mod portal, pure drop-in-zip, Farming-Sim-clone preset. Ship first.
2. **Space Engineers (244850)** — big Workshop scene, clean AppData mod folder, preset-only.
3. **Cities: Skylines (255710)** — huge Workshop catalog; preset-only (note native-vs-Proton path).
4. **Colony Survival (366090)** — trivial `gamedata/mods` install-dir target, preset-only.
5. **Timberborn (1062090)** — active Thunderstore scene; two-deployer preset (native + BepInEx).
6. **Civilization VI (289070)** — large catalog; preset-only with native-path caveat.
7. **No Man's Sky (275850)** — preset-only deploy, but manual `DISABLEMODS.TXT` step + pak version churn.

Moderate (ship with caveats): **tModLoader (1281930)** (native-Linux path ambiguity), **Palworld (1623730)** (pak easy / UE4SS needs setup). Skip: **Terraria 105600** (fold into tModLoader), **House Flipper 613100** (Workshop-only, no manageable mod folder).

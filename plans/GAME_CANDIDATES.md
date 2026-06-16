# Game support candidates — research & shipped presets

Derived from a Steam-library scan (55 installed games) + multi-agent web research. All "easy adds" are **preset-only — no C++ recompile** — using the existing **Case Matching Deployer** (`hard link`), the same pattern Limo already ships for Stardew/Bethesda/Farming-Sim. Raw briefs: [`research/games/`](research/games/).

> **Profile scope note:** the public Steam profile game list is private and the keyless `GetAppList` API was unavailable, so "owned-but-not-installed" titles below come from general research, not a full library dump. For a complete owned-library sweep, provide a Steam Web API key (https://steamcommunity.com/dev/apikey) or set the profile's game details to public.

Path tokens: `$STEAM_INSTALL_PATH$` = `steamapps/common/<installdir>`; `$STEAM_PREFIX_PATH$` = `compatdata/<appid>/pfx/drive_c`.

## Shipped this session (26 new presets)

### From your installed library
| App ID | Game | Deployer target | Notes |
|---|---|---|---|
| 227300 | Euro Truck Simulator 2 | prefix `Documents/Euro Truck Simulator 2/mod` | `.scs` drop-in, `no_extract` |
| 284160 | BeamNG.drive | prefix `AppData/Local/BeamNG/BeamNG.drive/current/mods` | `.zip` drop-in, `no_extract`; `current` path is v0.37+ (older = versioned folder) |
| 427520 | Factorio | prefix `AppData/Roaming/Factorio/mods` | `.zip` drop-in, `no_extract` |
| 805550 | Assetto Corsa Competizione | prefix `Documents/.../Customs` | liveries/skins only (UE4 signed paks) |
| 1153410 | JDM: Japanese Drift Master | `$INSTALL$/JDM/Content/Paks/~mods` | UE5 pak; optional signature-bypass is a user step |
| 255710 | Cities: Skylines | prefix `AppData/Local/Colossal Order/Cities_Skylines/Addons/Mods` | native-Linux users use the XDG path |
| 244850 | Space Engineers | prefix `AppData/Roaming/SpaceEngineers/Mods` | Workshop covers most |
| 366090 | Colony Survival | `$INSTALL$/gamedata/mods` | clean install-dir target |
| 275850 | No Man's Sky | `$INSTALL$/GAMEDATA/MODS` | manual `DISABLEMODS.TXT` deletion needed once |
| 1281930 | tModLoader | prefix `Documents/My Games/Terraria/tModLoader/Mods` | `.tmod`, `no_extract`; **native-Linux build often uses `~/.local/share/...` instead** |
| 289070 | Sid Meier's Civilization VI | prefix `Documents/My Games/Sid Meier's Civilization VI/Mods` | `.modinfo` auto-tag; native build uses aspyr path |
| 1062090 | Timberborn | prefix `Documents/Timberborn/Mods` + `$INSTALL$/BepInEx/plugins` | two deployers (native + BepInEx) |
| 1623730 | Palworld | `$INSTALL$/Pal/Content/Paks/~mods` + `.../ue4ss/Mods` | pak drop-in easy; UE4SS is a manual prereq |
| 1593500 | God of War | `$INSTALL$` (root) | texpack/loose-file drop-in |
| 1774580 | STAR WARS Jedi: Survivor | `$INSTALL$/SwGame/Content/Paks/~mods` | UE pak drop-in |
| 976730 | Halo: The Master Chief Collection | `$INSTALL$` (root) | launch "Anti-Cheat Disabled"; Workshop covers most |
| 553850 | HELLDIVERS 2 | `$INSTALL$/data` | single/pre-numbered mods only — Limo doesn't auto-renumber `patch_N` |

### Top broad-sweep picks (not installed, high value)
| App ID | Game | Deployer target |
|---|---|---|
| 294100 | RimWorld | `$INSTALL$/Mods` |
| 892970 | Valheim | `$INSTALL$/BepInEx/plugins` |
| 1966720 | Lethal Company | `$INSTALL$/BepInEx/plugins` |
| 632360 | Risk of Rain 2 | `$INSTALL$/BepInEx/plugins` |
| 261550 | Mount & Blade II: Bannerlord | `$INSTALL$/Modules` (`SubModule.xml` auto-tag) |
| 220200 | Kerbal Space Program | `$INSTALL$/GameData` |
| 582010 | Monster Hunter: World | `$INSTALL$/nativePC` |
| 251570 | 7 Days to Die | `$INSTALL$/Mods` (`ModInfo.xml` auto-tag) |
| 392160 | X4: Foundations | `$INSTALL$/extensions` (`content.xml` auto-tag) |

(Plus FS 15/17/19/22/25 and Assetto Corsa shipped earlier this session.)

## Installed but NOT shipped
| Game | Why |
|---|---|
| Forza Horizon 5 | Anti-cheat; no official mod folder; runtime injectors only — ban risk |
| Horizon Forbidden West | Decima engine needs an external packer; no loose-file merge |
| STAR WARS Battlefront II | Frostbite/Frosty runtime patcher; no loose-file merge |
| Sea of Thieves | Mandatory EAC, online-only — not moddable |
| Marvel Rivals | PvP asset-hash checks; modding is bannable |
| Halo Infinite | EAC in campaign; ban risk, no tools |
| A.O.T. Wings of Freedom | No mod format/tools |
| House Flipper (613100) | Steam Workshop-only, no manageable loose-file folder |
| Rise of the Ronin | Moderate — works as root drop-in but depends on a third-party Mod Loader (can add later) |
| My Singing Monsters | Technically a root-overwrite preset, but tiny/low-value audience |
| Terraria (105600) | Modding lives entirely in tModLoader (shipped) — base-game preset is low value |

## Researched future candidates (verify subpath before shipping)
Hollow Knight (367520, Modding-API vs BepInEx subpath), Monster Hunter Rise (1229490, REFramework/FirstNatives prereq), Sons of the Forest (1326470, RedLoader vs BepInEx split), Project Zomboid (108600, native `$HOME/Zomboid/mods` path), Slay the Spire (646570) & Don't Starve Together (322330) (Workshop-dominated). **Skip:** Elden Ring (EAC + ModEngine2 launcher), Dragon's Dogma 2 (Fluffy `.pak` repacking). **Already special-cased:** Baldur's Gate 3 (BG3 deployer, `LIMO_WITH_LOOT`).

## Pattern summary
- **Drop-in archive** (game reads the zip): FS, ETS2, BeamNG, Factorio, tModLoader → Case Matching + `no_extract`.
- **Loose-file merge into a folder** (install dir or prefix): everything else → Case Matching, no flag.
- A **new C++ deployer** is only needed for special load-order/plugin-list/packer logic (Bethesda LOOT, BG3, Cyberpunk, Witcher 3 — all already exist).

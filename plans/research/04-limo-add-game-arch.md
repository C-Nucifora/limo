I have everything needed. Here is the brief.

---

# Adding a New Game to Limo — Source-Level Brief

## 0. The big picture

Limo has **no per-game enum or factory switch keyed on game identity**. A "supported game" is purely a data file: `steam_app_configs/<steam_app_id>.json`. The only code-level registry is the list of *deployer types* (`DeployerFactory`). So adding a game that fits an existing deployer is a **pure JSON drop-in, no recompile**; adding a game whose mod layout no current deployer handles requires a **new C++ deployer subclass** registered in two places.

Search dirs (`AddAppDialog::gameConfigSearchDirs`, addappdialog.cpp:170–186; mirrored in `MainWindow::initRootLevelConditions`, mainwindow.cpp:2034–2038): a user dir first, then the bundled `<prefix>/share/limo/steam_app_configs` (or literal `steam_app_configs` for a local build). Bundled via CMakeLists.txt:545–551.

---

## 1. The recipe + JSON schema

### Files to add/edit
- **To add a game that fits an existing deployer:** add ONE file, `steam_app_configs/<appid>.json`. Nothing else. It is auto-bundled by the existing `install(DIRECTORY steam_app_configs ...)` rules (CMakeLists.txt:545, 550) — no CMake edit, no recompile.
- **To add a game needing new deployment logic:** also add a `FooDeployer` class (.cpp/.h) under `src/core/` and register it (see §4).

### Preset JSON schema
Top-level keys (parsed in `AddAppDialog::initConfigForApp`, addappdialog.cpp:188–379, key constants addappdialog.h:43–72; plus `MainWindow::initRootLevelConditions`, mainwindow.cpp:2076–2082):

```jsonc
{
  "name": "Display Name",            // JSON_NAME, addappdialog.cpp:371
  "deployers": [                     // JSON_DEPLOYERS_GROUP
    {
      "type": "Case Matching Deployer",   // JSON_DEPLOYERS_TYPE  (mandatory; must be in DEPLOYER_TYPES)
      "name": "Data",                     // JSON_DEPLOYERS_NAME  (mandatory)
      "target_dir": "$STEAM_INSTALL_PATH$/Data",  // JSON_DEPLOYERS_TARGET (mandatory)
      "deploy_mode": "hard_link",         // JSON_DEPLOYERS_MODE  (mandatory)
      "source_dir": "$STEAM_INSTALL_PATH$/Data",  // JSON_DEPLOYERS_SOURCE (optional; only autonomous deployers)
      "uses_separate_dirs": false,        // JSON_DEPLOYERS_SEPARATE_DIRS (optional; ReverseDeployer)
      "update_ignore_list": false         // JSON_DEPLOYERS_UPDATE_IGNORE_LIST (optional; ReverseDeployer)
    }
  ],
  "auto_tags": [ ... ],              // JSON_AUTO_TAGS_GROUP
  "root_level_conditions": [ ... ]   // JSON_ROOT_LEVEL_KEY (read only by MainWindow, not AddAppDialog)
}
```

Mandatory deployer keys: `type, name, target_dir, deploy_mode` (`JSON_DEPLOYER_MANDATORY_KEYS`, addappdialog.h:65–68).

**`deploy_mode`** (addappdialog.cpp:304–322): lowercased and `_`→space normalized, so `hard_link`/`hard link` → `Deployer::hard_link`; `sym_link`/`soft link`/`sym link` → `sym_link`; `copy` → `copy`. Both underscore and space spellings are accepted (the bundled presets mix them).

**Token resolution** (addappdialog.cpp:280–281, 328–330):
- `$STEAM_INSTALL_PATH$` → game install dir = `<library>/steamapps/common/<installdir>` (read from the appmanifest `installdir`, importfromsteamdialog.cpp:202–226).
- `$STEAM_PREFIX_PATH$` → `<library>/steamapps/compatdata/<appid>/pfx/drive_c` (importfromsteamdialog.cpp:291–296). If the game has no Proton prefix, the token stays unresolved.
- `$HOME$` → home dir (source paths only, addappdialog.cpp:330).
- A missing **target** dir is auto-created (`create_directories`, addappdialog.cpp:283–301); a missing **source** dir causes that deployer to be **silently dropped** (addappdialog.cpp:332–339). (A separate GOG code path, addappdialog.cpp:811–927, skips deployers whose paths use `$STEAM_PREFIX_PATH$` when no prefix is supplied.)

### `auto_tags`
Auto-applied tags that classify staged mods by their file layout. Parsed by `AutoTag(Json::Value)` (autotag.cpp:27–60). Schema:
- `name` — tag label.
- `expression` — boolean combinator over the conditions by index, e.g. `"0"`, `"0or1"`, `"0and1"`, `"0or1or2or3or4"` (validated by `TagConditionNode::expressionIsValid`, autotag.cpp:58).
- `conditions[]` each: `condition_type` = `"path"` or `"file_name"` (autotag.cpp:53; `path` = match against full relative path, `file_name` = basename only), `invert` (bool), `use_regex` (bool), `search_string`.

They are recommendations only (e.g. tag SKSE/plugins/archives), do not affect deployment, and are imported via a checkbox.

### `root_level_conditions`
Auto-detects the "root level" — how many leading directory components to strip when installing a mod so files land at the right depth (e.g. a `.esp` must end up directly in `Data/`, not `RandomFolder/Data/`). Parsed by `RootLevelCondition(Json::Value)` (rootlevelcondition.cpp:22–54). NOTE: **only `MainWindow::initRootLevelConditions` reads this key** (mainwindow.cpp:2076–2082); `AddAppDialog` ignores it. They feed `AddModDialog` to pre-set the root-level spinbox. Fields:
- `matcher_type` — `"simple"` or `"regex"` (rootlevelcondition.cpp:27–32).
- `target_type` — `"any"` | `"file"` | `"directory"`; only matching entries are considered (rootlevelcondition.cpp:34–42).
- `case_invariant` (bool, default false), `stop_on_branch` (bool, default true), `level_offset` (int, default 0), `expression` (the matcher pattern) (rootlevelcondition.cpp:45–53).

### Every supported `type` string (DeployerFactory, deployerfactory.h:12–37, constructed deployerfactory.cpp:23–49)
| `type` string | Class | What it does |
|---|---|---|
| `Simple Deployer` | `Deployer` | Links/copies every file from each enabled mod's staged dir into `target_dir`, backing up/restoring overwritten files. The base behavior. |
| `Case Matching Deployer` | `CaseMatchingDeployer` | Same, but first renames mod files/dirs whose name matches a target (or another enabled mod) file case-insensitively, so case-mismatched mods deploy correctly on case-sensitive Linux FS. |
| `Loot Deployer` | `LootDeployer` | Manages Bethesda plugin load order (`plugins.txt`/`loadorder.txt`) via LOOT. `source_dir`=where plugins install, `target_dir`=dir holding the txt files. Compile-gated on `LIMO_WITH_LOOT`. |
| `Reverse Deployer` | `ReverseDeployer` | Moves files *not* managed by another deployer out of `target_dir` and links them back; tracks externally-created files / per-profile saves. Honors `uses_separate_dirs`, `update_ignore_list`. |
| `OpenMW Plugin Deployer` | `OpenMwPluginDeployer` | LOOT-style plugin management writing to `openmw.cfg`. `LIMO_WITH_LOOT` only. |
| `OpenMW Archive Deployer` | `OpenMwArchiveDeployer` | Manages `.bsa` archive entries in `openmw.cfg`. `LIMO_WITH_LOOT` only. |
| `Baldurs Gate 3 Deployer` | `Bg3Deployer` | Manages `.pak` plugins + `modsettings.lsx` load order. |
| `Witcher 3 Deployer` | `Tw3Deployer` | Subclass of CaseMatching; rewrites each top-level `mod*` folder name with a 4-digit load-order prefix (`modFoo`→`mod0003_Foo`) so the game's alphabetical order matches Limo's load order. |
| `Cyberpunk 2077 Deployer` | `CyberpunkDeployer` | Prefixes each `.archive` under `archive/pc/mod/` with a load-order index for first-wins ordering. |
| `Overlay Deployer` | `OverlayDeployer` | Presents merged mods via a fuse-overlayfs union mount; never physically writes the game dir. Linux-only. |

`AUTONOMOUS_DEPLOYERS` map (deployerfactory.h:102–112) marks which manage their own files (Loot/OpenMW/BG3 = true) — these are the ones that take a `source_dir`. Note: BG3 and OpenMW have classes but **no bundled preset** in `steam_app_configs/` yet.

---

## 2. Which existing deployer fits FS and AC

### (a) Farming Simulator — drop whole `.zip` into `mods/`, game reads the zip
**Already fully supported — no new deployer needed.** The mechanism is on the *installer* side, not the deployer:
- The base `Deployer` walks each mod's staged dir with `recursive_directory_iterator` and links/copies **individual files** (`getModFiles`, deployer.cpp:795–807; `deployFiles`, deployer.cpp:654+). If a mod is staged as a single `.zip` file, that `.zip` is deployed verbatim as one opaque file.
- `Installer` has a `no_extract` flag (installer.h:32, installer.cpp:148–162) that copies the source archive into the staging dir **without extracting** — literally documented "Some games load mods as archive files directly (e.g. Doom source ports load .pk3/.pk4 zip archives as-is)." FS `.zip` mods are the identical pattern.

So FS = a `Case Matching Deployer` (or `Simple Deployer`) with `target_dir` pointing at the prefix `mods/` folder (e.g. `$STEAM_PREFIX_PATH$/users/steamuser/Documents/My Games/FarmingSimulator20XX/mods`), and users install each mod with the "Install archive without extracting" option (`no_extract`). The only gap is convenience (see §3).

### (b) Assetto Corsa — merge `content/` subtree into game root, case-insensitive, inter-mod conflicts
**Case Matching Deployer covers this.** AC's needs map exactly onto `CaseMatchingDeployer`:
- Subtree merge into game root → `target_dir = $STEAM_INSTALL_PATH$`, mods staged with their `content/...` layout deploy file-by-file, merging into existing `content/` (the base deployer merges directories; it links files, not whole trees).
- Case-insensitive paths → `adaptDirectoryFiles` + `adaptLoadorderFiles` (casematchingdeployer.cpp:67–199) rename mod files/dirs to match the existing target case **and** reconcile case across enabled mods (the `file_name_map`, casematchingdeployer.cpp:150–195) — this is precisely the case-folding AC needs.
- Inter-mod conflicts → handled generically by `Deployer::updateConflictGroups` (deployer.cpp:823+) and load-order priority (later in load order wins), surfaced in the conflicts UI. Not deployer-specific.

So AC = a single `Case Matching Deployer`, `target_dir = $STEAM_INSTALL_PATH$`, `deploy_mode hard_link`. A JSON-only addition.

---

## 3. Gaps and where new logic would live

- **Assetto Corsa:** no code gap. Preset-only. (Optionally add `auto_tags` for `content/cars`, `content/tracks`, etc., and `root_level_conditions` to auto-detect when a mod is zipped as `MyMod/content/...` vs `content/...`.)

- **Farming Simulator:** no *deployment* gap — works today with `Case Matching Deployer` + the `no_extract` install option. The only real gap is **ergonomics/defaulting**: `no_extract` is a per-mod-install checkbox (installer.h:46; chosen in `AddModDialog`, addappdialog/addmoddialog UI option groups) and is **not part of the app-config JSON schema** — there's no preset field to say "this game's mods should default to no-extract." If a frictionless "drop-in zip" experience is wanted, the minimal change is to add an optional per-deployer/app flag in the preset schema (e.g. `"default_install_flags": ["no_extract"]`) parsed in `AddAppDialog::initConfigForApp` and threaded into `AddModDialog`'s default option selection. No new deployer class is required — the deployer side already deploys a single archive file correctly. If you nonetheless wanted a dedicated "drop-in" deployer, it would be a trivial subclass of `Deployer`/`CaseMatchingDeployer` (mirror `Tw3Deployer`'s structure, tw3deployer.h:54+) but it would add nothing over the existing path.

Bottom line: **both FS and AC are achievable with existing deployers; neither strictly requires new deployer logic.** Any new game that needs genuinely new behavior should mirror `Tw3Deployer`/`CyberpunkDeployer` (subclass `CaseMatchingDeployer`, override `deploy()`), located in `src/core/`.

---

## 4. Game-specific hardcoding that adding a game touches

For a JSON-only game (FS, AC): **nothing is hardcoded** — no enum, no factory switch, no game-id branch. Confirmed: the only `app_id ==`/`steam_app_id ==` comparisons in the codebase are UI current-app index checks (e.g. mainwindow.cpp:1135, 2786) and the `steam_app_id == -1` "no app" guard (mainwindow.cpp:2031), not game dispatch.

For a game requiring a **new deployer type**, you must touch exactly these code points (all in deployerfactory.*):
1. `DeployerFactory::FOODEPLOYER` string constant — deployerfactory.h:12–37.
2. Add it to `DEPLOYER_TYPES` — deployerfactory.h:43–56 (the validator at addappdialog.cpp:269 rejects any `type` not in this list, so an unregistered type is silently dropped).
3. Add a `DEPLOYER_DESCRIPTIONS` entry — deployerfactory.h:58–99.
4. Add an `AUTONOMOUS_DEPLOYERS` entry — deployerfactory.h:102–112.
5. Add the `else if(type == FOODEPLOYER)` branch in `makeDeployer` + `#include` — deployerfactory.cpp:23–49, 1–12.
6. New `foodeployer.cpp/.h` in `src/core/` (mirror `tw3deployer.h`/`cyberpunkdeployer.h`).

The installer flag enum (`Installer::Flag`, installer.h:25–33) is the other piece of game-relevant hardcoding, but it is generic (`no_extract` etc.) and already covers the FS use case.

### Key file references
- Preset examples: `/home/christian/Documents/GitHub/limo/limo/steam_app_configs/489830.json` (Bethesda: 2× Case Matching + Loot + auto_tags + root_level_conditions), `292030.json` (Witcher 3 special deployer), `264710.json`/`413150.json` (minimal single-deployer non-Bethesda).
- `/home/christian/Documents/GitHub/limo/limo/src/core/deployerfactory.h` and `.cpp` — the only deployer-type registry/switch.
- `/home/christian/Documents/GitHub/limo/limo/src/ui/addappdialog.cpp:188-379` — `initConfigForApp` (preset → `EditDeployerInfo`/auto_tags, token resolution).
- `/home/christian/Documents/GitHub/limo/limo/src/ui/mainwindow.cpp:2028-2098` — `initRootLevelConditions` (the only reader of `root_level_conditions`).
- `/home/christian/Documents/GitHub/limo/limo/src/core/casematchingdeployer.cpp` — covers AC's case-folding + cross-mod reconciliation.
- `/home/christian/Documents/GitHub/limo/limo/src/core/installer.cpp:148-162` and `installer.h:25-57` — `no_extract`, covers FS's drop-in-zip.
- `/home/christian/Documents/GitHub/limo/limo/src/core/tw3deployer.h` — template to mirror for any future special deployer.

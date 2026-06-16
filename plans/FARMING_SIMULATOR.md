# Plan: Farming Simulator support (FS15 / FS17 / FS19 / FS22 / FS25)

Research: [`research/01-farming-sim-ecosystem.md`](research/01-farming-sim-ecosystem.md), [`research/04-limo-add-game-arch.md`](research/04-limo-add-game-arch.md).

## 1. How FS modding works (the facts that drive the design)

- A mod is a **single `.zip`** dropped into one `mods/` folder. The game reads the zip directly — **never extracted, never merged**. `modDesc.xml` must be at the zip root.
- There is **no engine load order** and no file-overwrite resolution between mods. Conflicts are content-level (two mods adding the same item) and resolved by the user simply not enabling both.
- **Which mods a save uses** is chosen per-savegame via in-game checkboxes and recorded in that save's `careerSavegame.xml`. Limo should **not** write that file — the `mods/` folder is a shared global pool; the game decides per-save.
- Filenames matter: avoid `-`/`.` (other than `.zip`); preserve original filenames (don't rewrite).

### Mods folder location (Linux / Proton)

`$STEAM_PREFIX_PATH$` resolves to `<library>/steamapps/compatdata/<appid>/pfx/drive_c` (verified against shipped presets, e.g. Fallout 3 uses `$STEAM_PREFIX_PATH$/users/steamuser/Local Settings/...`). So:

```
$STEAM_PREFIX_PATH$/users/steamuser/Documents/My Games/FarmingSimulator<YEAR>/mods
```

| Game | Steam App ID | `<YEAR>` folder |
|---|---|---|
| Farming Simulator 15 | 313160 | FarmingSimulator2015 |
| Farming Simulator 17 | 447020 | FarmingSimulator2017 |
| Farming Simulator 19 | 787860 | FarmingSimulator2019 |
| Farming Simulator 22 | 1248130 | FarmingSimulator2022 |
| Farming Simulator 25 | 2300320 | FarmingSimulator2025 |

> **Caveat to verify on a real prefix:** some older titles (FS15/17/19) historically created `My Documents/My Games/...` instead of `Documents/My Games/...` inside the prefix (Wine aliases them, but the literal dir created has varied). The deployer auto-creates a missing `target_dir`; if a tester finds the `My Documents` variant, ship that path for the affected versions. The `mods/` folder may not exist until the game has launched once — Limo creating it is fine.

## 2. Deployer choice — no new C++ needed

FS deploys the **whole `.zip` as one opaque file** into `mods/`. The base `Deployer` already links/copies individual staged files verbatim, so a mod staged as a single `.zip` (via the `no_extract` install flag) is deployed correctly. Use a **`Case Matching Deployer`** (or `Simple Deployer`) with `target_dir` = the prefix `mods/` path.

> auto_tags / root_level_conditions do **not** apply to FS: with `no_extract` the staged mod is just `FS22_Foo.zip`, so Limo can't see `modDesc.xml` inside it. Presets therefore carry only the deployer.

### Ready-to-ship preset — `steam_app_configs/1248130.json` (FS22)

```json
{
  "name": "Farming Simulator 22",
  "deployers": [
    {
      "type": "Case Matching Deployer",
      "name": "Mods",
      "target_dir": "$STEAM_PREFIX_PATH$/users/steamuser/Documents/My Games/FarmingSimulator2022/mods",
      "deploy_mode": "hard_link"
    }
  ]
}
```

Produce the same file for each version, changing `name`, the file name (`<appid>.json`), and the `FarmingSimulator<YEAR>` segment:
`313160.json` (2015), `447020.json` (2017), `787860.json` (2019), `1248130.json` (2022), `2300320.json` (2025).

These drop into `steam_app_configs/`, are auto-bundled by the existing `install(DIRECTORY steam_app_configs ...)` rule (`CMakeLists.txt:545,550`), and appear immediately in the Add-App template combo + Steam import picker with **no recompile**.

## 3. Ergonomics: default `no_extract` per preset (the one code change)

Today `no_extract` is a per-mod-install checkbox (`installer.h` flag; chosen in `AddModDialog`), **not** part of the preset schema — so an FS user must remember to tick "install archive without extracting" on every mod. Without it, Limo would extract the zip and deploy its loose contents, which FS will **not** load.

**Change:** add an optional preset field, e.g.

```json
"default_install_flags": ["no_extract"]
```

- Parse it in `AddAppDialog::initConfigForApp` (`src/ui/addappdialog.cpp:188-379`) into the app/deployer config.
- Thread it to `AddModDialog` so the relevant install option group defaults to `no_extract` for this game.
- Keep it user-overridable per mod.

Small, localized, backward-compatible (absent field → current behavior). This is what makes FS "just work."

## 4. Sources

- **Primary:** manual `.zip` import from any source (ModHub, KingMods, GitHub, Discord). Universal, safe, ToS-clean.
- **Modpacks:** need **no special handling** — a modpack is just one (large) `.zip`, or several zips; importing them is identical to importing single mods.
- **ModHub auto-download:** experimental/low-priority — see [MODHUB_DOWNLOAD.md](MODHUB_DOWNLOAD.md).

## 5. "Different mods per map" / "multiple packs at once"

These are the modpack features — see [MODPACKS.md](MODPACKS.md). Short version: per-map mod sets map onto **profiles** (one per map); "multiple packs active at once" needs the new tag-backed **pack toggle** feature. Ship the FS preset together with the pack feature for the full experience.

## 6. Work breakdown

1. Add 5 preset JSON files (data only). — *Issue #229*
2. Add `default_install_flags` preset field + thread `no_extract` default into `AddModDialog`. — *Issue #230*
3. (Pairs with) pack toggles + per-map profiles — *Issue #232*.
4. Test on a real Proton prefix: confirm the `Documents` vs `My Documents` path per version; confirm a deployed hard-linked `.zip` is read by the game.

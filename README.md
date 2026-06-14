<h1 align="center">Limo <img src="resources/logo.png" alt="logo" width="40"/></h1>

A native Linux mod manager, primarily developed for Linux with support for the [NexusMods](https://www.nexusmods.com/) API and [LOOT](https://loot.github.io/).

<p align="center">
<img src="resources/showcase.png" alt="Limo main window" width="800"/>
</p>

This is a personal hard fork of [limo-app/limo](https://github.com/limo-app/limo) (upstream). It adds native deployers for **The Witcher 3: Wild Hunt** and **Cyberpunk 2077**, migrates to **Qt6**, and restructures the build so that neither libloot nor libunrar is required by default.

---

## What this fork adds over upstream

| Area | Detail |
|---|---|
| **The Witcher 3 deployer** | `Tw3Deployer` — encodes Limo load order into `mod{NNNN}_` folder-name prefixes so TW3's alphabetical loading rule stays in sync with Limo's list. |
| **Cyberpunk 2077 deployer** | `CyberpunkDeployer` — targets the game root; prepends `{NNNN}_` to `.archive` filenames under `archive/pc/mod/` so top-of-list wins, leaves all other paths (CET, RED4ext, redscript, TweakXL…) untouched. |
| **No-sudo build** | `LIMO_WITH_LOOT=OFF` (default) and `LIMO_WITH_UNRAR=OFF` (default) gate out libloot and libunrar entirely. The only non-packaged dependency is **cpr**, built from source into a local prefix — no `sudo make install`. |
| **Qt6** | Migrated from Qt5 to Qt6; Qt5 fallback retained in CMake. |
| **Palette-aware theme** | A modern application theme that respects the system light/dark palette. |
| **CI** | GitHub Actions workflow on Ubuntu 24.04 / g++-14, building with LOOT and UNRAR off. |
| **Integrated upstream PRs** | [#225](https://github.com/limo-app/limo/pull/225), [#208](https://github.com/limo-app/limo/pull/208), [#191](https://github.com/limo-app/limo/pull/191), [#199](https://github.com/limo-app/limo/pull/199) |
| **Bug fixes** | Deployer source path wiped on type change (#151); upstream bugs #180/#98/#138; use-after-end crashes in `sortModsByConflicts`, `getDeployerInfo`, `swapMod`, `setModStatus`; segfault on version-group removal; deployer cleanup loops. |
| **Security fixes** | Symlink-exfiltration block on deploy; sanitized download/NXM inputs; hardened archive extraction and FOMOD path handling. |

---

## Supported games

Beyond the games upstream already supports (Skyrim/SSE/VR, OpenMW, Baldur's Gate 3, and any generic game via Case Matching Deployer), this fork adds:

| Game | Steam App ID | Guide |
|---|---|---|
| The Witcher 3: Wild Hunt | 292030 | [docs/WITCHER3.md](docs/WITCHER3.md) |
| Cyberpunk 2077 | 1091500 | [docs/CYBERPUNK2077.md](docs/CYBERPUNK2077.md) |

Both games are auto-configured on Steam import via `steam_app_configs/292030.json` and `steam_app_configs/1091500.json`.

---

## How Limo works

### Staging Directory

When you install a mod, Limo stores all its files in a *Staging Directory* rather than touching the game immediately. This lets you freely reorder, enable, or disable mods without copying files back and forth.

### Deployers

A *Deployer* takes mods from the Staging Directory and links them into the game's directory (the *Target Directory*). Each deployer manages its own load order; a mod lower in the list wins any conflict. The fork adds two new deployer types on top of upstream's existing ones (Case Matching, LOOT, OpenMW, BG3, Reverse).

For a full usage guide, see the upstream [wiki](https://github.com/limo-app/limo/wiki).

---

## Build

Full recipe: **[docs/BUILDING.md](docs/BUILDING.md)**

Quick reference (no sudo, LOOT and UNRAR off):

```bash
# 1. Build cpr into a local prefix
git clone --depth 1 https://github.com/libcpr/cpr /tmp/cpr
cmake -S /tmp/cpr -B /tmp/cpr/build -G Ninja \
  -DCPR_USE_SYSTEM_CURL=ON -DCPR_BUILD_TESTS=OFF \
  -DBUILD_SHARED_LIBS=ON -DCMAKE_INSTALL_PREFIX=/tmp/cpr-prefix
cmake --build /tmp/cpr/build --target install

# 2. Build Limo
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DLIMO_WITH_LOOT=OFF \
  -DLIMO_WITH_UNRAR=OFF \
  -DCMAKE_PREFIX_PATH=/tmp/cpr-prefix
cmake --build build -j"$(nproc)"

# 3. Run without installing
LD_LIBRARY_PATH=/tmp/cpr-prefix/lib ./build/Limo
```

### CMake options

| Option | Default | Effect |
|---|---|---|
| `LIMO_WITH_LOOT` | `OFF` | Compile LOOT and OpenMW deployers. Requires libloot (`loot/api.h` + `libloot.so`). |
| `LIMO_WITH_UNRAR` | `OFF` | Compile RAR-archive extraction. Requires libunrar. |
| `LIMO_INSTALL_PREFIX` | `/usr/local` | Prefix for `cmake --install`. |
| `IS_FLATPAK` | `OFF` | Routes paths under `/app/` for Flatpak builds. |

The CI workflow (`.github/workflows/ci.yml`) runs on Ubuntu 24.04 with g++-14 and is the canonical reference for the expected compiler and flag set.

---

## Status and caveats

- The `Tw3Deployer` and `CyberpunkDeployer` implement the full load-order logic described in their respective guides and the source compiles cleanly under CI.
- **Neither deployer has been validated end-to-end in-game under Proton.** The load-order prefix logic is correct per the documented game rules, but edge cases (script conflicts in TW3, CET symlink behavior in CP2077) have not been exhaustively tested.
- Script merging (TW3) and REDmod compilation (CP2077) are out of scope — use a separate tool for those.
- See each guide for the full current-limitations section.

---

## Roadmap

- **In-game validation** of both new deployers under Proton on native Steam.
- **Wiring the TW3 config-merge helper** (`tw3configmerge`) and the **CP2077 Proton-setup helper** into the Limo UI.
- **REDmod integration** — invoking `redmod deploy` from within Limo for mods shipped as REDmod source projects.
- **Packaging** — AUR PKGBUILD and/or Flatpak manifest update for the fork.

---

## Upstream features

Everything in [limo-app/limo](https://github.com/limo-app/limo) is present:

- Multiple target directories per application
- Automatic case-mismatch adaptation
- Auto-tagging and filtering
- FOMOD installer support
- Conflict-based load order sorting
- Steam game import
- Backup system
- NexusMods API (updates, changelogs, downloads)
- LOOT integration — plugin management, auto sort, issue checking (requires `LIMO_WITH_LOOT=ON`)
- OpenMW support — plugins and `.bsa` archives (requires `LIMO_WITH_LOOT=ON`)
- BG3 deployer — `modsettings.lsx` management

---

## Contributing game configurations

Steam import configurations live in `steam_app_configs/<STEAM_APP_ID>.json`. To add or improve a configuration, export it from Limo's *App* tab (*Export* button → `exported_config.json` in the staging directory), rename it, and open a pull request.

---

## Attribution and license

Limo was created by [limo-app/limo](https://github.com/limo-app/limo) contributors and is licensed under the **GNU General Public License v3.0**. This fork carries the same license. See [LICENSE](LICENSE).

For the upstream project's usage wiki: [https://github.com/limo-app/limo/wiki](https://github.com/limo-app/limo/wiki).

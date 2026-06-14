# Fork notes

This is a personal fork of [limo-app/limo](https://github.com/limo-app/limo) that adds native
support for The Witcher 3 and Cyberpunk 2077 on Linux and fixes the build so it works without
installing any system-wide optional libraries.

---

## What this fork adds

### New game support

- **The Witcher 3: Wild Hunt** (Steam app 292030) — `Tw3Deployer` + Case Matching Deployer for
  `dlc/`. Load order is encoded into `mod*` folder-name prefixes so the game's alphabetical
  loading rule and Limo's list order stay in sync.
  See [docs/WITCHER3.md](WITCHER3.md).

- **Cyberpunk 2077** (Steam app 1091500) — `CyberpunkDeployer` targeting the game root. Load
  order for `.archive` files is encoded via `{NNNN}_` filename prefixes under
  `archive/pc/mod/`. All other file types (`red4ext/`, `r6/scripts/`, `bin/x64/plugins/`, etc.)
  are routed transparently by the root-targeting deployer.
  See [docs/CYBERPUNK2077.md](CYBERPUNK2077.md).

### No-sudo build

Upstream requires libloot and libunrar to be present at configure time. This fork makes both
optional:

- `-DLIMO_WITH_LOOT=OFF` (default) — skips the LOOT and OpenMW deployers entirely; libloot is not
  needed.
- `-DLIMO_WITH_UNRAR=OFF` (default) — skips RAR-archive extraction; libunrar is not needed.

The only non-packaged dependency is **cpr**, which you build from source into a local prefix
(no `sudo make install` required).
See [docs/BUILDING.md](BUILDING.md) for the full recipe.

### Integrated upstream PRs

The following upstream pull requests from `limo-app/limo` have been merged into this fork's `dev`
branch:

| PR | Summary |
|---|---|
| [#225](https://github.com/limo-app/limo/pull/225) | Merged |
| [#208](https://github.com/limo-app/limo/pull/208) | Merged |
| [#191](https://github.com/limo-app/limo/pull/191) | Merged |
| [#199](https://github.com/limo-app/limo/pull/199) | Merged |

### Bug fixes

- Fixed deployer source path being wiped on type change (upstream issue #151).
- Fixed three upstream-reported bugs (#180, #98, #138).
- GCC 16 missing-include fixes.

---

## Documentation index

- [Building from source](BUILDING.md)
- [The Witcher 3 setup guide](WITCHER3.md)
- [Cyberpunk 2077 setup guide](CYBERPUNK2077.md)

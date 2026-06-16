# Limo — Farming Simulator & Assetto Corsa Support: Plans

Implementation plans drafted from multi-source research (web + codebase). Raw research briefs are in [`research/`](research/).

## The key architectural fact

Limo has **no per-game enum or factory switch** — a "supported game" is purely a data file `steam_app_configs/<steam_app_id>.json` declaring deployers + auto-tags. Adding a game that fits an existing deployer is a **pure JSON drop-in, no recompile** (`src/ui/addappdialog.cpp:188-379` parses presets; `src/core/deployerfactory.{h,cpp}` is the only deployer-type registry). This makes most of this work low-risk and data-driven.

## Plans & matching GitHub issues

| Plan | Scope | Code change? | Issue |
|---|---|---|---|
| [FARMING_SIMULATOR.md](FARMING_SIMULATOR.md) | Bundled presets for FS15/17/19/22/25 | **No** (data only) | #229 |
| [FARMING_SIMULATOR.md §Drop-in](FARMING_SIMULATOR.md#3-ergonomics-default-no_extract-per-preset) | Per-preset default install flags (`no_extract`) so FS mods deploy as the raw `.zip` | Small | #230 |
| [ASSETTO_CORSA.md](ASSETTO_CORSA.md) | AC (244210) preset + archive root normalization; ACC note | Mostly data | #231 |
| [MODPACKS.md](MODPACKS.md) | Toggleable "packs" (multiple active at once) + per-map profiles | Core + UI | #232 |
| [MODHUB_DOWNLOAD.md](MODHUB_DOWNLOAD.md) | ModHub paste-a-link download (experimental) + GitHub Releases import | Medium | #233 (low priority) |
| [SUPPORTED_GAMES_UI.md](SUPPORTED_GAMES_UI.md) | Supported-games gallery, Steam-picker "has preset" flag, empty-state showcase | UI | #234 |

## TL;DR feasibility

- **Farming Simulator (all 5 versions):** mods are whole `.zip` files dropped into the prefix `mods/` folder — *not extracted*. Works **today** with a Case-Matching/Simple deployer + the existing `no_extract` install flag. Presets are data-only; the only code change is defaulting `no_extract` per game (#230).
- **Assetto Corsa:** mods merge a `content/` tree into the install dir; "installed = on disk". A single Case-Matching deployer targeting `$STEAM_INSTALL_PATH$` covers it. Data-only, plus optional importer normalization for inconsistently-rooted archives.
- **Modpacks ("different mods per map", "multiple packs active"):** *different mods per map* = **profiles** (already exist). *Multiple packs active at once* is the genuine gap — no current primitive does union-of-many. Recommended: tag-backed "packs" with a toggle bar that drives enabled-state (Option A).
- **ModHub direct download:** technically possible (stable CDN URL, gated only by `User-Agent`+`Referer`, no auth) but fragile + ToS-ambiguous → **experimental / low priority**, exactly as suspected. Manual `.zip` import is the robust primary path and works for every source.
- **Other sources (KingMods, OverTake, etc.):** no public download APIs → manual import. GitHub Releases is the one clean automatable source.

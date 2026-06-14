# Modding Cyberpunk 2077 with Limo

Steam App ID: **1091500**

---

## Importing the game

Use **File > Import from Steam** and select Cyberpunk 2077 (app ID 1091500). Limo pre-configures
a single deployer from `steam_app_configs/1091500.json`:

| Deployer name | Type | Target directory | Deploy mode |
|---|---|---|---|
| Cyberpunk 2077 | Cyberpunk 2077 Deployer | `<game root>` (Steam install path) | hard link |

The deployer targets the **game root** rather than a subdirectory. Most Cyberpunk mods downloaded
from Nexus are packaged with their full path from the game root already baked in
(`archive/pc/mod/`, `bin/x64/plugins/`, `red4ext/plugins/`, `r6/scripts/`, `r6/tweaks/`,
`engine/`, etc.), so a root-targeting deployer routes every file to the correct location without
any per-file path rewriting for the general case.

---

## How `.archive` load order works

Cyberpunk loads `.archive` files from `archive/pc/mod/` in **alphabetical filename order**.
For conflicting resources, the **alphabetically first archive wins** — the opposite of The Witcher 3.

Limo's Cyberpunk 2077 Deployer (`CyberpunkDeployer`, `src/core/cyberpunkdeployer.cpp`) implements
a "top of the list wins" convention by prepending a numeric prefix to every `.archive` filename
at deployment time:

- A mod at load order index `i` (0-based, 0 = top of the list) has its archives deployed as
  `{i:04d}_<original filename>` inside `archive/pc/mod/`.
  - Example: `foo.archive` at position 0 becomes `0000_foo.archive`; at position 5 it becomes
    `0005_foo.archive`.
- Alphabetical order then equals load order, so the **top of the list gets prefix `0000_` and
  sorts first, winning all conflicts**.

Only files that satisfy both conditions get this treatment (from `cyberpunkdeployer.cpp`
`isOrderedArchive`):
1. The file lives **directly** inside `archive/pc/mod/` (not a subdirectory).
2. The file extension is `.archive` (case-insensitive).

All other files — including `.archive.xl` companion files (ArchiveXL), any files under
`archive/pc/mod/` subdirectories, and everything outside `archive/pc/mod/` — are deployed with
their original relative path unchanged.

### ArchiveXL companion files

Files like `foo.archive.xl` are matched to their archive by content, not by filename, so they
must **not** receive the numeric prefix. The deployer leaves them untouched, which is the correct
standard behaviour.

---

## CET, RED4ext, redscript, and other mod types

Because the deployer targets the game root and preserves all directory structure, these mod types
work automatically without special configuration:

| Mod type | Typical path inside mod archive |
|---|---|
| Cyber Engine Tweaks (CET) | `bin/x64/plugins/cyber_engine_tweaks/mods/<name>/` |
| ASI loader plugins | `bin/x64/plugins/` |
| RED4ext plugins | `red4ext/plugins/<name>/` |
| redscript | `r6/scripts/<name>/` |
| TweakXL tweaks | `r6/tweaks/<name>/` |
| REDmod archives | `archive/pc/mod/` (load-order prefixed) |
| ArchiveXL | `archive/pc/mod/` (`.archive.xl`, not prefixed) |

---

## Required Proton setup

Cyberpunk 2077 on Linux under Proton needs a few one-time steps before mods work reliably.

### 1. Steam launch option

In Steam > Cyberpunk 2077 > Properties > Launch Options, set:

```
WINEDLLOVERRIDES="winmm,version=n,b" %command%
```

This enables the CET and RED4ext hooks that most script mods depend on.

### 2. Proton dependencies via protontricks

Install the required Windows runtimes into the game's Proton prefix:

```bash
protontricks 1091500 d3dcompiler_47 vcrun2022
```

### 3. Deploy mode: hard link on the same filesystem

The deployer defaults to **hard link** mode. You **must** keep the Limo staging directory on the
same filesystem as the game directory — hard links cannot span filesystem boundaries.

**Do not use sym link mode for Cyberpunk 2077.** Cyber Engine Tweaks (CET) does not follow
symlinks correctly; using symlinks will cause CET and mods that depend on it to fail silently or
not load at all.

If your staging directory is on a different filesystem than the game, either:
- Move the staging directory onto the same partition, or
- Use **copy** mode (slower to deploy, but no filesystem restriction and no symlink issues).

---

## Current limitations

- The archive prefix uses 4 decimal digits (`0000_` to `9999_`). More than 10 000 `.archive` mods
  in a single deployer would overflow the prefix — far beyond any realistic setup.
- REDmod (CDPR's official mod compiler) is not invoked by Limo. If a mod ships as a REDmod source
  project rather than a pre-compiled `.archive`, you need to run `redmod deploy` separately or
  use the pre-compiled archive variant of that mod.
- The load order only affects `.archive` conflict resolution. CET mod load order (if relevant) is
  still managed inside CET's own settings.

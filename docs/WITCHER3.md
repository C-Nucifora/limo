# Modding The Witcher 3 with Limo

Steam App ID: **292030**

---

## Importing the game

Use **File > Import from Steam** (or the Steam import button) and select The Witcher 3: Wild Hunt
(app ID 292030). Limo pre-configures two deployers from `steam_app_configs/292030.json`:

| Deployer name | Type | Target directory | Deploy mode |
|---|---|---|---|
| Mods | Witcher 3 Deployer | `<game>/Mods/` | hard link |
| DLC | Case Matching Deployer | `<game>/dlc/` | hard link |

The **Mods** deployer handles everything the game loads from its `Mods/` directory (bundled content,
scripts, textures). The **DLC** deployer handles fan-made DLC placed in the `dlc/` directory and
uses the base `CaseMatchingDeployer` since no load-order prefixing is needed there.

---

## How load order works

The Witcher 3 (next-gen) scans `Mods/` and loads every direct subfolder whose name starts with
`mod` in **alphabetical order of the folder name**. When two mods provide conflicting bundled
resources, **the alphabetically-later folder wins** (last loaded overrides earlier ones).

Limo's Witcher 3 Deployer (`Tw3Deployer`, `src/core/tw3deployer.cpp`) translates Limo's load order
list directly into alphabetical folder order:

- A mod at position `i` (0-based, where 0 is the top of the list) has its top-level `mod*` folder
  deployed as `mod{i:04d}_<OriginalSuffix>`.
  - Example: `modFoo` at position 3 is deployed as `mod0003_Foo`.
- **A mod lower (later) in the list gets a larger index, an alphabetically-later folder name,
  and therefore wins any conflict with mods above it.**

In other words, the **bottom of the list has the highest priority** — the same "last wins" rule
that TW3MM, Vortex, and the game itself use.

### Mods with no `mod*` top-level folder

If a staged mod contains no top-level folder starting with `mod`, Limo wraps everything in a
synthesized `mod{i:04d}_{mod_id}` folder so the game still recognizes it. This covers mods
packaged as bare `content/` trees.

Files not under a `mod*` folder (e.g. loose files at the `Mods/` root) are deployed unchanged.

---

## The DLC deployer

Use the **DLC** deployer (type: Case Matching Deployer, target: `<game>/dlc/`) for fan-made DLC
mods. These do not require load-order prefixing — the game loads them by content type rather than
alphabetical priority — so a plain `CaseMatchingDeployer` is correct.

---

## Deploy mode note

Both deployers default to **hard link** mode. Hard links require the staging directory and the game
directory to be on the **same filesystem**. If they are on different filesystems, switch to **sym
link** or **copy** mode in the deployer settings.

---

## Current limitations

- The load-order prefix uses 4 decimal digits (`mod0000_` to `mod9999_`). Setups with more than
  10 000 mods in a single deployer would overflow the prefix, but this is far beyond any realistic
  TW3 setup.
- Mods that place loose files directly at the `Mods/` root (not under a `mod*` folder) may behave
  unexpectedly when the fallback synthesized folder is used; this edge case is noted in the source
  (`tw3deployer.cpp`, line 118 TODO comment) and has not been verified against the game.
- Script merging (the equivalent of TW3MM's Script Merger) is not handled by Limo; conflicting
  script files still need to be merged manually or with a separate tool.
- The DLC deployer does not reorder files; if two DLC mods conflict, the one lower in the list
  wins by the standard Limo rule (lower = higher priority), but the game may not respect that
  ordering the same way it does for `mod*` folders.

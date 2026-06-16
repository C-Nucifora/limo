I now have enough to write the brief. Battlefront II requires Frosty (a runtime-launcher .fbmod patcher, not loose-file deploy) — that's not a clean Limo fit. Let me confirm the exact Steam install folder names for God of War and Helldivers 2 since those drive the deployer path correctness, then compile.

The key facts are established. God of War installdir is "God of War", Helldivers 2 is "Helldivers 2". Let me compile the final brief.

# Limo Game-Support Assessment — AAA Action / RPG / Shooter Batch

Scope notes: Limo adds games via a `steam_app_configs/<appid>.json` preset only (no recompile) when a game fits an existing deployer. For these games the only relevant fit is the **Simple Deployer** or **Case Matching Deployer** hard-linking loose mod files / pak archives into a target dir under `$STEAM_INSTALL_PATH$` (the install dir) or `$STEAM_PREFIX_PATH$` (Proton drive_c). Where a game requires an external runtime patcher/launcher (Frosty, HFW packer, Helldivers patch-numbering) or a console/anti-cheat lockout, Limo can at best stage files but cannot replicate the proprietary pack/patch step — flagged accordingly.

---

### God of War (1593500) — Moddable: **Yes**
- **Ecosystem:** Nexus Mods (`nexusmods.com/godofwar`); Vortex extension exists. Active retexture/gameplay scene.
- **Format & location:** Two mod styles. (1) `.texpack`/`.lodpack` texture mods + loose files that go in the game **root** (next to the exe) — or a user-created `Mods` folder processed by a Python "Mod Importer". (2) Vortex installs straight to the game root. Target dir: `$STEAM_INSTALL_PATH$` (Steam installdir `God of War`).
- **Deployer fit:** **Case Matching Deployer** → game root. PRESET-ONLY for the drop-in `.texpack`/`.lodpack` + loose-file mods (the common case). The optional `modimporter.py` flow (modfile.txt patching) is an external step Limo doesn't run, but most popular GoW mods are direct drop-ins, so a root-targeted deployer covers the bulk.
- **Verdict: Easy add.** High priority — single-player, no anti-cheat, large Nexus catalog, clean loose-file/texpack drop-in into the game root.

```json
{
  "name": "God of War",
  "deployers": [
    {
      "type": "Case Matching Deployer",
      "name": "Game Files",
      "target_dir": "$STEAM_INSTALL_PATH$",
      "deploy_mode": "hard link"
    }
  ],
  "auto_tags": []
}
```

---

### Horizon Forbidden West (2420110) — Moddable: **Yes, but via a custom packer**
- **Ecosystem:** Nexus Mods (`nexusmods.com/horizonforbiddenwest`); Vortex extension + "ModForge"/"HFW Mod Manager".
- **Format & location:** Decima engine. Mods are **not** plain drop-in files — they must be **packed/patched** by a custom tool ("requires a custom mod packer (patcher)") into the game's archive format before the game reads them. Game dir is `$STEAM_INSTALL_PATH$` but the packed output isn't a simple file merge.
- **Deployer fit:** No existing Limo deployer reproduces the Decima pack step. Limo could stage raw mod files but the required packing is external.
- **Verdict: Hard (needs external packer integration).** Low priority for Limo — skip until/unless a "no-pack" loose-file path exists.

---

### STAR WARS Jedi: Survivor (1774580) — Moddable: **Yes**
- **Ecosystem:** Nexus Mods (`nexusmods.com/starwarsjedisurvivor`); UE4 pak mods + an optional "R457 Mod Loader".
- **Format & location:** Unreal Engine `.pak`/`.ucas`/`.utoc` trio. Drop into a user-created `~mods` subfolder of the Paks dir: `$STEAM_INSTALL_PATH$/SwGame/Content/Paks/~mods` (installdir `Jedi Survivor`). The game auto-loads everything under Paks.
- **Deployer fit:** **Case Matching Deployer** → `.../Content/Paks/~mods`, with the existing **`no_extract`** install flag for whole-pak mods. PRESET-ONLY. (UE pak drop-in is the same pattern Limo already uses successfully for other UE games.)
- **Verdict: Easy add.** High priority — single-player, no anti-cheat, standard UE `~mods` pak drop-in.

```json
{
  "name": "STAR WARS Jedi: Survivor",
  "deployers": [
    {
      "type": "Case Matching Deployer",
      "name": "Pak Mods",
      "target_dir": "$STEAM_INSTALL_PATH$/SwGame/Content/Paks/~mods",
      "deploy_mode": "hard link"
    }
  ],
  "auto_tags": []
}
```

---

### Rise of the Ronin (1340990) — Moddable: **Limited but real**
- **Ecosystem:** Nexus Mods (`nexusmods.com/riseoftheronin`), small but growing catalog; a "Rise of the Ronin Mod Loader" and an ASI fix plugin exist.
- **Format & location:** KT/Omega Force engine. Mods are loaded via a community **Mod Loader** + ASI plugins; retexture mods exist. Files go in/near the game dir `$STEAM_INSTALL_PATH$` (installdir `Rise of the Ronin`), but most content mods depend on the third-party Mod Loader being installed to the game root.
- **Deployer fit:** **Case Matching Deployer** → game root can stage the Mod Loader + its mod files. PRESET-ONLY to deploy files, but the user must install the Mod Loader/ASI loader themselves (an external dependency, similar to ScriptExtender-style setups Limo already coexists with).
- **Verdict: Moderate.** Medium priority — works as a root drop-in deployer, but ecosystem is young and Mod-Loader-dependent.

```json
{
  "name": "Rise of the Ronin",
  "deployers": [
    {
      "type": "Case Matching Deployer",
      "name": "Game Files",
      "target_dir": "$STEAM_INSTALL_PATH$",
      "deploy_mode": "hard link"
    }
  ],
  "auto_tags": []
}
```

---

### STAR WARS Battlefront II (1237950) — Moddable: **Yes, but via Frosty (runtime patcher)**
- **Ecosystem:** Nexus Mods (`nexusmods.com/starwarsbattlefront22017`), very large catalog; Vortex extension exists **but only stages files** — actual loading requires **Frosty Mod Manager**.
- **Format & location:** Frostbite engine `.fbmod` files. Frosty Mod Manager **patches game files in-memory at launch** and you must **launch through Frosty** — there is no loose-file/pak merge into the game dir the engine reads directly. ModData lives at `$STEAM_INSTALL_PATH$/ModData` but it's Frosty-generated, not hand-mergeable.
- **Deployer fit:** Limo can stage `.fbmod` files into Frosty's mods folder, but Frosty's apply/patch + custom launch is outside Limo's deployers. Not a clean preset.
- **Verdict: Hard (Frosty-dependent).** Low priority — also note multiplayer; mods are single-player/skin focused. Skip unless a Frosty-staging convenience deployer is desired.

---

### Sea of Thieves (1172620) — Moddable: **No**
- **Ecosystem:** None meaningful. Always-online live-service title.
- **Blocker:** **Easy Anti-Cheat is mandatory** (confirmed by Rare) and the game is online-only; client file tampering is blocked/bannable. No mod scene beyond cheats.
- **Verdict: Not moddable.** Do not add.

---

### Helldivers 2 (553850) — Moddable: **Yes (cosmetic), with a caveat**
- **Ecosystem:** Nexus Mods (`nexusmods.com/helldivers2`); HD2ModManager / HD2 Arsenal. Developer (Arrowhead) tolerates client-side cosmetic mods but disallows anything affecting other players/cheating.
- **Format & location:** Stingray/Autodesk archive patches. Mods are dropped into `$STEAM_INSTALL_PATH$/data` (installdir `Helldivers 2`), overwriting/adding `patch` files — **but** they must be named in a sequential `patch_N` numbering scheme so the game loads them, which mod managers handle automatically.
- **Deployer fit:** **Case Matching Deployer** → `$STEAM_INSTALL_PATH$/data` works for staging, but the **sequential patch renumbering** is the catch: Limo's deployer hard-links files as-named and does not auto-renumber conflicting `patch_N` files. Single mods / pre-numbered mods work as a drop-in; multiple overlapping mods need the rename logic only the dedicated manager provides.
- **Verdict: Moderate.** Medium priority — easy/PRESET-ONLY for single or pre-numbered mods into `/data`; multi-mod patch-numbering is the limitation.

```json
{
  "name": "HELLDIVERS 2",
  "deployers": [
    {
      "type": "Case Matching Deployer",
      "name": "Data Patches",
      "target_dir": "$STEAM_INSTALL_PATH$/data",
      "deploy_mode": "hard link"
    }
  ],
  "auto_tags": []
}
```

---

### Marvel Rivals (2767030) — Moddable: **No**
- **Ecosystem:** Cosmetic UE mods existed early but are now blocked.
- **Blocker:** Online PvP only; Season 1 added **asset hash checking** that detects file tampering and disables client mods, and modding is officially a **bannable offense**. No safe path.
- **Verdict: Not moddable.** Do not add.

---

### Halo Infinite (1240440) — Moddable: **Limited / risky**
- **Ecosystem:** Nexus Mods (`nexusmods.com/haloinfinite`) — mostly loading-screen/video swaps and a few campaign overhauls. No official tools (unlike MCC).
- **Blocker:** **Easy Anti-Cheat runs even in single-player campaign.** Mods work in some cases but there is real ban risk, and 343/Halo Studios provides no mod tools or support.
- **Verdict: Not recommended / borderline Not moddable.** Low priority — anti-cheat-gated; skip.

---

### Halo: The Master Chief Collection (976730) — Moddable: **Yes (officially supported)**
- **Ecosystem:** **Steam Workshop (appid 976730)** with official support, plus Nexus Mods (`nexusmods.com/halothemasterchiefcollection`), ModDB, and "Assembly" tooling. Must launch via the **"Play … Anti-Cheat Disabled"** option for mods.
- **Format & location:** Steam Workshop handles subscriptions automatically. For manual Nexus mods, files go under the game dir `$STEAM_INSTALL_PATH$` (installdir `Halo The Master Chief Collection`); a one-click mod utility exists.
- **Deployer fit:** Workshop mods need no Limo management (Steam handles them). For Nexus/manual mods a **Case Matching Deployer** → `$STEAM_INSTALL_PATH$` works, PRESET-ONLY. Main value of a Limo preset is managing manual/Nexus mods alongside Workshop content.
- **Verdict: Easy add.** Medium-high priority — officially mod-friendly (anti-cheat toggle), but note Steam Workshop already covers most users, so Limo's added value is for manual/Nexus mods.

```json
{
  "name": "Halo: The Master Chief Collection",
  "deployers": [
    {
      "type": "Case Matching Deployer",
      "name": "Game Files",
      "target_dir": "$STEAM_INSTALL_PATH$",
      "deploy_mode": "hard link"
    }
  ],
  "auto_tags": []
}
```

---

### A.O.T. / Attack on Titan: Wings of Freedom (449800) — Moddable: **No (effectively)**
- **Ecosystem:** No Steam Workshop, no official tools. Only trainers/cheats (PLITCH, FLiNG) and a mouse-input fix. Community consensus: "the game isn't moddable."
- **Deployer fit:** Nothing for Limo to manage (no content-mod format).
- **Verdict: Not moddable.** Do not add.

---

### My Singing Monsters (\appid in set: 1419170) — Moddable: **Limited (informal asset replacement)**
- **Ecosystem:** GameBanana, a couple of Steam Workshop-listed guide items, Discord/Reddit. No official mod support; many "mods" are cheat/mod-menu tools.
- **Format & location:** Modding is **manual asset file replacement** — swapping texture/audio files inside the game's install folder (`steamapps/common/...` → game asset files). No mod-folder/load-order system; mods overwrite originals in place.
- **Deployer fit:** This is exactly the in-place overwrite pattern. **Case Matching Deployer** → `$STEAM_INSTALL_PATH$` (with a Reverse-Deployer angle since mods replace existing files). PRESET-ONLY and technically clean, BUT the ecosystem is tiny and reskin-oriented; value is low.
- **Verdict: Easy (technically) but low priority.** Could ship a preset, but the audience is small.

```json
{
  "name": "My Singing Monsters",
  "deployers": [
    {
      "type": "Case Matching Deployer",
      "name": "Game Files",
      "target_dir": "$STEAM_INSTALL_PATH$",
      "deploy_mode": "hard link"
    }
  ],
  "auto_tags": []
}
```

---

## Top easy adds (ranked) for this set
1. **STAR WARS Jedi: Survivor (1774580)** — standard UE `~mods` pak drop-in, big single-player Nexus scene, clean preset + `no_extract` flag. Best ROI.
2. **God of War (1593500)** — texpack/lodpack + loose-file drop-in into game root, large single-player catalog, no anti-cheat.
3. **Halo: MCC (976730)** — officially mod-friendly (anti-cheat-disabled launch), root drop-in for Nexus/manual mods (Workshop already covers the rest).
4. **Helldivers 2 (553850)** — easy `/data` drop-in for single/pre-numbered mods; only caveat is multi-mod patch renumbering.
5. **My Singing Monsters (1419170)** — technically trivial in-place overwrite preset, but small/low-value audience (include only if casting a wide net).

**Moderate (defer):** Rise of the Ronin (Mod-Loader-dependent). 
**Hard / skip:** Horizon Forbidden West (custom Decima packer), Battlefront II 2017 (Frosty runtime patcher). 
**Not moddable:** Sea of Thieves (EAC), Marvel Rivals (hash-checked PvP, bannable), Halo Infinite (campaign EAC, ban risk), A.O.T. Wings of Freedom (no mod support).

Sources: [God of War – PCGamingWiki](https://www.pcgamingwiki.com/wiki/God_of_War), [GoW Mod Importer/Mods folder (GitHub)](https://github.com/MagicGonads/sgg-mod-format/wiki/Installing-Mods-%7C-The-Mods-Folder), [GoW Nexus](https://www.nexusmods.com/godofwar), [HFW custom packer (Nexus ModForge/HFW Mod Manager)](https://www.nexusmods.com/horizonforbiddenwest/mods/137), [Jedi Survivor ~mods/Paks install (VULKK)](https://vulkk.com/2023/12/25/star-wars-jedi-survivor-recommended-game-mods/), [Jedi Survivor Nexus](https://www.nexusmods.com/starwarsjedisurvivor), [Rise of the Ronin Mod Loader (Nexus)](https://www.nexusmods.com/riseoftheronin/mods/9), [Battlefront II Frosty usage (modding.wiki)](https://modding.wiki/en/starwarsbattlefront22017/users), [Sea of Thieves EAC (Halo/Rare support)](https://support.seaofthieves.com/articles/17668960344850-Steam-Launch-Error-Easy-Anti-Cheat-is-not-installed), [Helldivers 2 /data manual install (Linux guide)](https://steamcommunity.com/sharedfiles/filedetails/?id=3486261798), [Helldivers2 Mod Manager getting started (DeepWiki)](https://deepwiki.com/teutinsa/Helldivers2ModManager/2-getting-started), [Marvel Rivals modding (Siliconera)](https://www.siliconera.com/can-you-mod-marvel-rivals/), [Marvel Rivals hash check/bannable (Sportskeeda)](https://www.sportskeeda.com/esports/are-using-marvel-rivals-mods-bannable-offense-possibilities-explored), [Halo Infinite campaign anti-cheat (PCGamingWiki)](https://www.pcgamingwiki.com/wiki/Marvel_Rivals), [Halo MCC Workshop modding (Halo Support)](https://support.halowaypoint.com/hc/en-us/articles/28916975982868-How-to-Download-and-Play-Mods-for-Halo-The-Master-Chief-Collection-via-the-Steam-Workshop), [A.O.T. Wings of Freedom – PCGamingWiki](https://www.pcgamingwiki.com/wiki/Attack_on_Titan), [My Singing Monsters modding guide (Steam)](https://steamcommunity.com/sharedfiles/filedetails/?id=3027365216), [My Singing Monsters – PCGamingWiki](https://www.pcgamingwiki.com/wiki/My_Singing_Monsters)

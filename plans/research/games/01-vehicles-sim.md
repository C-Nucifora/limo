I now have all the key facts confirmed. The JDM install dir is `steamapps/common/JDM/JDM/Content/Paks/~mods`, mods are loose pak/utoc/ucas files. Let me do one final confirmation on the BeamNG userfolder behavior under Proton (does it follow the prefix's LOCALAPPDATA) and check the ACC custom folder is the deployer target. I have enough to write the brief. Let me verify the BeamNG path maps cleanly to the Proton prefix.

The BeamNG userfolder is `%LOCALAPPDATA%\BeamNG\BeamNG.drive\current\mods` which under Proton maps to `$STEAM_PREFIX_PATH$/users/steamuser/AppData/Local/BeamNG/BeamNG.drive/current/mods`. I have everything I need. Let me write the brief.

# Limo Game-Add Assessment: Driving / Racing / Vehicle / Simulation

Scope: 6 titles assigned. Facts verified via BeamNG/SCS official docs, Nexus Mods, PCGamingWiki, ProtonDB, and Steam (sources at end). Path tokens: `$STEAM_INSTALL_PATH$` = `steamapps/common/<installdir>`; `$STEAM_PREFIX_PATH$` = `compatdata/<appid>/pfx/drive_c`. Note that for several of these games the "Documents"/"AppData" mod folders live **inside the Proton prefix**, i.e. under `$STEAM_PREFIX_PATH$/users/steamuser/...`.

---

### Euro Truck Simulator 2 (227300) — Moddable: **YES** (excellent, first-class mod manager)
- **Ecosystem:** Huge. Steam Workshop + Nexus + ETS2 community sites (ets2modding, modhub). SCS officially supports modding with an in-game Mod Manager.
- **Mod format & install location:** Mods are `.scs` archives (the game's own package format) placed **as-is, no extraction**, into the user's `mod` folder. Under Proton this is the in-prefix Documents folder:
  `$STEAM_PREFIX_PATH$/users/steamuser/Documents/Euro Truck Simulator 2/mod`
  Workshop items live separately under `steamapps/workshop/content/227300`, but the manual `mod` folder is the clean deployer target.
- **Limo fit:** Drop-in archive pattern (exactly like Farming Simulator, which Limo already ships). **Case Matching Deployer** + the existing `no_extract` install flag → target the `mod` folder. **PRESET-ONLY.**
- **Verdict:** **Easy add.** Highest-priority pick in this set — large, active, officially-sanctioned mod scene with a stable archive format and a well-defined folder.

```json
{
  "name": "Euro Truck Simulator 2",
  "deployers": [
    {
      "type": "Case Matching Deployer",
      "name": "Mods",
      "target_dir": "$STEAM_PREFIX_PATH$/users/steamuser/Documents/Euro Truck Simulator 2/mod",
      "deploy_mode": "hard link"
    }
  ],
  "auto_tags": []
}
```
(Install `.scs` mods with the `no_extract` flag so the archive is deployed whole.)

---

### BeamNG.drive (284160) — Moddable: **YES** (extremely mod-centric)
- **Ecosystem:** Massive. Official in-game Repository + Nexus + community. Mods are core to the game.
- **Mod format & install location:** Mods are **`.zip` archives placed as-is, do NOT extract** (per BeamNG docs). They go in the `mods` subfolder of the **user folder**, which as of v0.37+ is `%LOCALAPPDATA%\BeamNG\BeamNG.drive\current\mods`. Under Proton:
  `$STEAM_PREFIX_PATH$/users/steamuser/AppData/Local/BeamNG/BeamNG.drive/current/mods`
  (Caveat: BeamNG explicitly warns against storing the user folder inside the install dir, so the prefix AppData path is the correct, update-safe target — not `$STEAM_INSTALL_PATH$`.)
- **Limo fit:** Drop-in archive pattern again. **Case Matching Deployer** + `no_extract` → target the `mods` folder. **PRESET-ONLY.**
- **Verdict:** **Easy add** (one small caveat: the `current` path segment changed at v0.37 and is version-stable now, but older installs used `0.3x` versioned folders — document this). Very high priority given BeamNG's mod volume.

```json
{
  "name": "BeamNG.drive",
  "deployers": [
    {
      "type": "Case Matching Deployer",
      "name": "Mods",
      "target_dir": "$STEAM_PREFIX_PATH$/users/steamuser/AppData/Local/BeamNG/BeamNG.drive/current/mods",
      "deploy_mode": "hard link"
    }
  ],
  "auto_tags": []
}
```
(Install `.zip` mods with `no_extract`.)

---

### Assetto Corsa Competizione (805550) — Moddable: **LIMITED but clean** (liveries/skins only; UE4 sim, no Workshop)
- **Ecosystem:** Custom liveries are the dominant mod type (OverTake, RaceDepartment/overtake.gg, dedicated livery sites). No Steam Workshop. ACC is UE4 with signed paks, so deep car/track mods are not really a thing — but the official **Customs** livery system is fully supported by the game.
- **Mod format & install location:** Loose files — a car `.json` plus a livery folder containing `decals.png` / `sponsors.png`. They go into the in-prefix Documents Customs folders:
  `$STEAM_PREFIX_PATH$/users/steamuser/Documents/Assetto Corsa Competizione/Customs/Cars` (JSON) and `.../Customs/Liveries/<name>` (folders).
- **Limo fit:** Loose-file tree merge into a stable folder. **Case Matching Deployer** (Linux case-folding matters for the png/json names) targeting the `Customs` root, letting mod packs that ship `Cars/` + `Liveries/` subfolders merge in. **PRESET-ONLY.**
- **Verdict:** **Easy add** (scope-limited to liveries). Good companion to Limo's existing Assetto Corsa preset; lower mod *diversity* than ETS2/BeamNG but trivially handled.

```json
{
  "name": "Assetto Corsa Competizione",
  "deployers": [
    {
      "type": "Case Matching Deployer",
      "name": "Custom Liveries",
      "target_dir": "$STEAM_PREFIX_PATH$/users/steamuser/Documents/Assetto Corsa Competizione/Customs",
      "deploy_mode": "hard link"
    }
  ],
  "auto_tags": []
}
```

---

### JDM: Japanese Drift Master (1153410) — Moddable: **YES** (active UE5 pak scene on Nexus)
- **Ecosystem:** Nexus Mods has an active, growing JDM section (cars, handling, unlocks). Devs permit personal-use mods. Nexus's own app lists it.
- **Mod format & install location:** UE5 game. Mods are loose `.pak` / `.utoc` / `.ucas` files dropped into the game's Paks folder, conventionally the `~mods` subfolder:
  `$STEAM_INSTALL_PATH$/JDM/Content/Paks/~mods`
  (install dir is `steamapps/common/JDM`, then `JDM/Content/Paks/~mods`). Many mods also require a one-time "UTOC Signature Bypass Patch" pak that itself just drops into the same Paks folder.
- **Limo fit:** Loose-file merge into a fixed folder — textbook **Case Matching Deployer** (or Simple) to `~mods`, `deploy_mode` hard link. No load-order plugin list to manage (UE5 `~mods` is alphabetical/implicit), so **no new C++ deployer needed. PRESET-ONLY.**
- **Verdict:** **Easy add.** Clean UE5 `~mods` pattern; the only friction is the optional signature-bypass prerequisite (a user step, not a Limo concern). Solid mid-priority pick.

```json
{
  "name": "JDM: Japanese Drift Master",
  "deployers": [
    {
      "type": "Case Matching Deployer",
      "name": "Paks (~mods)",
      "target_dir": "$STEAM_INSTALL_PATH$/JDM/Content/Paks/~mods",
      "deploy_mode": "hard link"
    }
  ],
  "auto_tags": []
}
```

---

### Forza Horizon 5 (1551360) — Moddable: **LIMITED / risky** (anti-cheat; no official mod support)
- **Ecosystem:** A community exists (Forza Mods on GitHub, Nexus FH5 section) but it's largely *runtime* tooling — trainers, Cheat-Engine-style tools, and an experimental sound/visual mod loader. There is **no official mod folder or supported mod format**; mods either inject at runtime or replace files in the install dir.
- **Anti-cheat:** Forza ships built-in anti-cheat; the online/connected experience actively resists modification and using mod tools risks bans. On Linux/Proton the anti-cheat is workaround-dependent and version-fragile (ProtonDB).
- **Limo fit:** Limo deploys files on disk; it can't manage runtime injectors, EAC interactions, or the ban risk. There's no stable, sanctioned target_dir or package format to model. A file-replacement Reverse Deployer could *technically* point at the install dir, but that's fragile and anti-cheat-hostile.
- **Verdict:** **Not a good add / Hard, low priority.** Skip unless a stable file-based mod-loader convention emerges. Recommend **do not add** for now.

---

### Sim Racing Telemetry (845210) — Moddable: **NO (not a game)**
- This is a **standalone telemetry capture/analysis utility** that records lap data from *other* sim racing titles. It has no mods, no mod folder, no mod ecosystem.
- **Limo fit:** None — nothing to deploy.
- **Verdict:** **Not moddable / out of scope.** Exclude from Limo. (Appears to have been included by appid pattern-matching; it's not a moddable game.)

---

## Top easy adds (ranked, this set)

1. **Euro Truck Simulator 2 (227300)** — `.scs` drop-in archives → `Documents/Euro Truck Simulator 2/mod`; huge officially-supported scene; mirrors the Farming Sim preset Limo already ships. **Highest priority.**
2. **BeamNG.drive (284160)** — `.zip` drop-in (`no_extract`) → prefix `AppData/Local/BeamNG/.../current/mods`; enormous mod volume. Minor path-version note. **Very high priority.**
3. **JDM: Japanese Drift Master (1153410)** — loose UE5 paks → `$STEAM_INSTALL_PATH$/JDM/Content/Paks/~mods`; active Nexus scene; clean Case Matching preset. **High/mid priority.**
4. **Assetto Corsa Competizione (805550)** — loose livery JSON/PNG → prefix `Documents/.../Customs`; easy but livery-only scope. **Mid priority.**

Skip: **Forza Horizon 5 (1551360)** — anti-cheat + no stable mod-file convention (Hard/low). **Sim Racing Telemetry (845210)** — not a moddable game (out of scope).

All four "easy adds" are **preset-only** (data JSON, no recompile) and reuse the existing Simple/Case Matching Deployer plus the `no_extract` install flag for the two archive-based games (ETS2, BeamNG).

Sources: [BeamNG userfolder docs](https://documentation.beamng.com/support/userfolder/), [BeamNG install-mods docs](https://documentation.beamng.com/tutorials/mods/installing-mods/), [SCS ETS2 mod-folder forum](https://forum.scssoft.com/viewtopic.php?t=189824), [ets2modding install guide](https://www.ets2modding.com/install-ets-2-mods/), [SCS Mod Manager wiki](https://modding.scssoft.com/wiki/Documentation/Engine/Mod_manager), [ACC custom liveries tutorial (OverTake)](https://www.overtake.gg/news/tutorial-how-to-run-custom-liveries-in-acc.1580/), [simracingsetup ACC liveries](https://simracingsetup.com/assetto-corsa/where-to-find-acc-liveries/), [JDM Nexus Mods](https://www.nexusmods.com/games/jdmjapanesedriftmaster/mods), [JDM UTOC bypass / Paks path (Nexus)](https://www.nexusmods.com/jdmjapanesedriftmaster/mods/4), [JDM Steam modding discussion](https://steamcommunity.com/app/1153410/discussions/1/600780367643819248/), [Forza Mods (GitHub)](https://github.com/forzamods), [FH5 Nexus](https://www.nexusmods.com/games/forzahorizon5), [FH5 ProtonDB](https://www.protondb.com/app/1551360), [Sim Racing Telemetry on Steam](https://store.steampowered.com/app/845210/Sim_Racing_Telemetry/).

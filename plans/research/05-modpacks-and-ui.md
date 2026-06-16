I have a complete picture. Here is the brief.

---

# Limo: Modpack UX (Farming Simulator) & Supported-Games View — Design Brief

## Part 1 — Modpacks: which existing primitive fits, what's missing

### The three existing primitives

**Profiles** (`ModdedApplication::addProfile/setProfile/removeProfile`, `profile_names_`, `current_profile_` in `moddedapplication.h:1176-1178`; mirrored per-deployer in `deployer.h:651-659`).
- A profile is a complete, independent **load order + enabled-state + conflict-groups snapshot, per deployer**. Each `Deployer` holds `std::vector<...> loadorders_` indexed by profile (`deployer.h:654`), so switching profiles swaps the entire enabled/disabled set and order at once.
- **Exactly one profile is active at a time** (`current_profile_` is a single int). Switching is mutually exclusive.
- Profiles are exportable/importable as bundles (`exportProfile`/`importProfile`, `PROFILE_BUNDLE_FILE_NAME`).
- Mod *membership* (which mods exist, groups, tags, notes, colours, categories) is **instance-wide, not profile-scoped** — only the load order/enabled-state/conflict-groups are per-profile. Confirmed by `importProfile`'s doc note that "groups are an instance-wide concept managed independently of profiles" (`moddedapplication.h:1100-1106`).

**Groups** (`groups_`, `group_map_`, `active_group_members_`, `group_names_`, `group_notes_` in `moddedapplication.h:1180-1188`; `createGroup`, `addModToGroup`, `changeActiveGroupMember`).
- A group is a set of mods where **only the active member deploys** — it's a version/variant selector (e.g. "Texture mod v1 vs v2"). Deployment honors this: `getModInfo` marks `is_active = active_group_members_[group] == mod.id` (`moddedapplication.cpp:721`), and `updateDeployerGroups` swaps which member is in the load order.
- Mutually exclusive **by construction** — one active member per group. This is the opposite of "multiple active at once."

**Tags** (`ManualTag`/`AutoTag` over base `Tag`, `manual_tag_map_`, `auto_tag_map_`).
- Tags are pure **labels for filtering/grouping in the UI**. `Tag` just holds a name + a list of mod ids (`tag.h:53-56`). They have **zero deployment effect** — they only drive the filter proxies (`ModListProxyModel::filter_tags`, `tag_filters_` at `modlistproxymodel.h:31,157`; deployer-side `DeployerListProxyModel::filter_tags`). AutoTags are derived from file contents via `TagConditionNode`; ManualTags are user-assigned.
- A mod can carry **many tags simultaneously** — tags are non-exclusive and overlap freely.

### Mapping to the two FS requirements

| Requirement | Best-fit primitive | Why / gap |
|---|---|---|
| **"Different mods per map/save"** | **Profiles** | This is exactly what a profile *is*: a self-contained enabled-set + load order. One profile per map ("Erlengrat mods", "Frutiger Berg mods") already works today via the profile selector. **Gap:** profiles are heavyweight (full load-order snapshots) and the UI presents them as a single dropdown with New/Remove/Edit — there's no notion of "this profile = this save," no per-save auto-switching, and creating one per map is manual. |
| **"MULTIPLE packs active simultaneously"** | **Nothing fits cleanly** | This is the real gap. Profiles are single-select (`current_profile_` is one int). Groups are explicitly single-active. Tags are multi-but-inert (no deploy effect). There is **no primitive that lets you toggle several named, overlapping bundles on/off and have the union deploy.** |

### What's missing, concretely

The missing concept is a **"pack": a named, user-defined set of mods that can be toggled on/off, where any number of packs can be on at once, and the deployed set is the *union* of all enabled packs** (within the current profile). Today:

1. **No multi-select activation.** `setProfile(int)` and `getProfile()` are scalar. Enabled-state lives as a single bool per load-order entry per profile (`DeployerEntry` in the tree). There is no "this mod is enabled *because* pack X is on."
2. **Tags can't drive deployment.** `Tag` has no enabled flag and nothing in `deployMods`/`updateDeployerGroups` consults tags. Tags reach only the proxy models.
3. **No union semantics.** Groups give you *intersection-to-one* (pick one). Nothing gives you *union-of-many*.

### Smallest feature that delivers it

There are two viable shapes; I recommend **Option A** for the smallest honest delivery, with **Option B** as the richer follow-up.

**Option A — "Pack = a tag with a toggle that drives enabled-state" (smallest).**
Reuse `ManualTag` as the storage for pack membership (a pack is just a named tag, and a mod can be in several packs because tags overlap). Add a per-deployer-per-profile set of "active pack names." When the active-pack set changes, recompute each mod's enabled bit as `enabled = (mod is in ≥1 active pack)` and write it into the existing load-order tree, then deploy as normal.
- **Why it's small:** membership UI already exists (`managemodtagsdialog`, `editmanualtagsdialog`, tag assignment via `addTagsToMods`). Deployment already keys off the per-entry enabled bool — you're only *computing* that bool from pack state instead of from a manual click.
- **New surface:** a "Packs" bar of multi-checkable toggles next to the deployer/profile selectors (mirror the existing tag-filter checkbox column built from `TagCheckBox`, but checking a pack box *enables those mods* rather than *filtering the view*). Persist active-pack names per profile in `json_settings_`.
- **Honest limitation:** load *order* between packs still comes from the single underlying per-profile load order; packs decide on/off, not relative order. For FS (where load order rarely matters vs. Bethesda games) this is acceptable.

**Option B — "Pack as a first-class concept layered over tags/groups" (richer).**
Add a `packs_` structure to `ModdedApplication` (parallel to `groups_`): `std::vector<Pack>` where `Pack { name; notes; std::set<int> mod_ids; bool enabled; }`, plus `pack_map_`. Persist in `updateSettings`/`updateState`. Deployment unions all enabled packs. This is the clean model but touches serialization, the deploy path, and needs its own manage dialog.

**Recommendation:** Ship **A** (tag-backed packs + a toggle bar that sets enabled-state). It delivers "multiple packs active at once" with the least new code and degrades gracefully into B later (B is just promoting the tag to a struct with an `enabled` flag and union-deploy). For "different mods per map," lean on **profiles as-is** and only add sugar: a "duplicate profile" affordance and clearer naming — no core change needed.

### Files/classes touched (Part 1)

- **Core model:** `src/core/moddedapplication.{h,cpp}` — add active-pack state (Option A: `std::vector<std::set<std::string>> active_packs_per_profile_` or similar; Option B: `packs_`/`pack_map_`), a `setPackEnabled(name, bool)` method, recompute-enabled logic feeding the existing load-order tree, and serialize in `updateSettings`/`updateState`. Profile-add/remove must extend the per-profile pack vector (mirror `addProfile`/`removeProfile` at `deployer.cpp:363-393`).
- **Deploy path:** `deployMods`/`updateDeployerGroups` (`moddedapplication.cpp`) — apply computed enabled bits before deploy; reuse `setModStatusAcrossDeployers` semantics for split mods.
- **Tags as membership (Option A):** reuse `ManualTag`, `addTagsToMods`/`removeTagsFromMods`, `manual_tag_map_`.
- **UI:** `src/ui/mainwindow.cpp` — new "Packs" toggle bar near `profile_selection_box`/`deployer_selection_box` (build from `TagCheckBox` analog, `src/ui/tagcheckbox.{h,cpp}`); wire checks to a new `ApplicationManager` signal `setPackEnabled`. A "Manage Packs" dialog modeled on `src/ui/managegroupsdialog.{h,cpp}` and `src/ui/managemodtagsdialog.{h,cpp}`.
- **Signals plumbing:** `src/ui/applicationmanager.{h,cpp}` (mirror existing `setProfile`/`addProfile` connects in `mainwindow.cpp:463-474`).

---

## Part 2 — Supported-games view: how games are shown today, and how to spruce it up

### How apps/games are presented today

- **Active apps** live in a single dropdown: `ui->app_selection_box` (a `QComboBox`), populated in `MainWindow::onGetAppNames` (`mainwindow.cpp:2326-2390`). Each item gets the app name and, if present, an icon via `addItem(QIcon(icon_paths[real_id]), names[real_id])`. Optional alphabetical sort (`sort_apps_alphabetically_`, fork for limo#226) maps display index → real id via `app_combo_id_map_`.
- **Empty state:** when no apps exist, a built-in overlay (`empty_state_overlay_`, `mainwindow.cpp:214-254`) shows "No applications yet" + an "Add application" button → `onAddAppButtonClicked`.
- **Adding a game** = `AddAppDialog` (`addappdialog.{h,cpp}`). It has: name/staging fields, an **"Advanced setup"** toggle (`setAdvancedMode`, fork #92), a **GOG game-template combo** (`gog_template_combo`, populated by `populateGogTemplateCombo` from bundled + user `steam_app_configs/*.json`), and an **"Import from Steam"** button → `ImportFromSteamDialog`.
- **Presets ("supported games")** are the `steam_app_configs/*.json` files (15 bundled today: Skyrim/SE/VR, Fallout 3/NV/4, Oblivion, Cyberpunk, Witcher 3, Stardew, Subnautica, etc. — **no Farming Simulator yet**). Each file is named `<steamAppId>.json` and carries `name`, `deployers[]`, `auto_tags[]`. They're discovered via `gameConfigSearchDirs()` (user `game_configs/` dir first, then bundled `steam_app_configs/`, fork #204). When importing from Steam, `initConfigForApp()` matches `<steam_app_id>.json` and auto-builds deployers + auto-tags; otherwise `initDefaultAppConfig()`.
- **Steam import dialog:** `ImportFromSteamDialog` parses `libraryfolders.vdf`, fills a `QTableWidget` (`ui->app_table`) with **icon + name + app_id + has-prefix + path** per installed game (`addTableRow`, `importfromsteamdialog.cpp:179-259`), with a search box. It does **not** indicate which installed games Limo has a preset for.

### Concrete UI ideas (each grounded in existing widgets)

**Idea 1 — "Supported games" gallery/grid replacing the bare combo + advanced form.**
Add a grid view of bundled presets to `AddAppDialog`: a `QListWidget` in `IconMode` (or a `QListView` over a small model) where each tile is one `steam_app_configs/*.json` — icon + `json["name"]`. The data source already exists: `populateGogTemplateCombo()` already walks every config and reads `JSON_NAME` (`addappdialog.cpp:718-776`); reuse that loop to build tiles instead of a flat combo. Clicking a tile = "one-click add from bundled preset": prefill name from the JSON, apply its deployers/auto-tags via the existing `initConfigForGog`/`initConfigForApp` path, and just ask for the install/staging dir. This turns the hidden GOG combo into the primary, visual entry point. **Touches:** `src/ui/addappdialog.{h,cpp}` + its `.ui` (new grid widget); reuses `gameConfigSearchDirs`, `JSON_NAME`, `initConfigForGog`.

**Idea 2 — "Has-preset" flag in the Steam library picker.**
In `ImportFromSteamDialog::addTableRow` (`importfromsteamdialog.cpp:179`), the row already has the Steam `app_id`. Add a column (or a badge/icon on the name cell) that checks whether `<app_id>.json` exists in `gameConfigSearchDirs()` — exactly the `initConfigForApp` lookup. Games Limo can auto-configure get a "Preset available" check/star; unknown games get a neutral marker. Optionally add a filter toggle "Only show games Limo supports." This directly answers "which installed games Limo has presets for." **Touches:** `src/ui/importfromsteamdialog.{h,cpp}` (new column + per-row preset check; the dir-search helper currently lives in `AddAppDialog::gameConfigSearchDirs` — lift it to a shared util or `ModdedApplication` static so both dialogs use it), and the dialog `.ui` for the extra column.

**Idea 3 — Visual app switcher to replace/augment the dropdown.**
`app_selection_box` already stores each app's icon path in `Qt::UserRole` (`mainwindow.cpp:2336`). Promote this into an optional **icon-grid sidebar** (a `QListView` in IconMode bound to the same name/icon arrays from `onGetAppNames`), so users pick games by cover art rather than a text dropdown. Keep the combo as the compact fallback. Reuse the existing `app_combo_id_map_` display→real-id mapping so sorting/selection logic is unchanged. **Touches:** `src/ui/mainwindow.cpp` (`onGetAppNames`, the selection plumbing around `currentApp()`/`app_combo_id_map_`) + main window `.ui`.

**Idea 4 — Richer empty state as a mini "supported games" showcase.**
Today's `empty_state_overlay_` is a title + one button (`mainwindow.cpp:214-254`). Replace the single button with a small grid of the most common bundled presets (same tile source as Idea 1) so a first-run user sees recognizable games and adds one in a click, falling through to the full `AddAppDialog` for everything else. **Touches:** `src/ui/mainwindow.cpp` (`setupEmptyStateOverlay` area).

**Idea 5 — Ship a Farming Simulator preset (data, not code).**
Drop a `steam_app_configs/<fsAppId>.json` (e.g. FS22 = `1248130`, FS25 = `2300320`) defining its deployer (mods go to `.../FarmingSimulator20XX/mods`). It immediately appears in the GOG template combo (Idea 1 grid) and gets flagged in the Steam picker (Idea 2) with **no code change**, because both surfaces are data-driven off `steam_app_configs/`. This also pairs naturally with Part 1: an FS profile-per-map plus pack toggles.

### Files/classes touched (Part 2)

- `src/ui/addappdialog.{h,cpp}` (+ `.ui`) — preset grid (Ideas 1, 4 source).
- `src/ui/importfromsteamdialog.{h,cpp}` (+ `.ui`) — has-preset column/badge + filter (Idea 2).
- `src/ui/mainwindow.cpp` (+ main `.ui`) — `onGetAppNames` grid switcher (Idea 3), empty-state showcase (Idea 4).
- Shared helper: lift `AddAppDialog::gameConfigSearchDirs()` (`addappdialog.cpp:160`) into a shared location so the Steam picker can reuse the same preset-discovery logic.
- Data only: new `steam_app_configs/*.json` for Farming Simulator (Idea 5).

### Cross-cutting note
Parts 1 and 2 reinforce each other: a bundled FS preset (Part 2, Idea 5) gives sensible deployers, and the pack-toggle bar (Part 1, Option A) is what makes FS's "multiple mod sets per map, several active at once" actually deployable — the two should ship together for the FS use case.

# Limo Fork — Implementation Plan

A step-by-step plan for the **46 open GitHub issues** (C-Nucifora/limo), drafted by inspecting the actual code. **Issues (defects) come first**, then **enhancements**, then cross-cutting appendices (roadmap, shared infrastructure, packaging/CI/testing).

**Already resolved & closed (13):** #3/#4/#5/#6/#9 (group/conflict/rules/notes/pin features), #21/#39/#40 (dialog + notes/pin wiring), #34 (REDmod shell-escaping), #48/#51/#52/#58 (TW3/CP game-tool wiring).

**How to read:** each entry has Goal · Validity · Approach · Touch points (`file:line`) · Considerations · Effort (S ≤1h / M a few hours / L a day+) · Risks · Depends-on. Start with the **Roadmap** below for ordering; effort/dependency calls are the planning agents' assessments and should be sanity-checked before execution.

---

## Roadmap & sequencing

**Priority principles.** Ship safety first: security holes (#37, #32, #30, #28) and correctness bugs (#43, #42, #41) before anything else, then low-cost UX fixes that make existing features usable (#36, #38, #26, #23, #25). Next, integrate the already-built-but-unwired agent branches early (#1/#2 collections, #7 export, #8 download queue, #10 separators) — high value per remaining effort. Only then net-new UX/workflow, online/Nexus depth, game-specific depth, and finally large infrastructure (CLI, foreign-manager import, save manager, i18n).

| Milestone | Theme | Issues | Why this order |
|---|---|---|---|
| **M0** | Security, correctness bugs & blocking UX | #37, #32, #30, #28, #43, #42, #41, #36, #38, #26, #23, #25 | Defects, not features: untrusted-input memory safety (#37, #30), command injection (#32, completing the #34 family), key confidentiality (#28), deploy/rules/pin bugs (#43/#42/#41), silent/blank-state UX (#36/#38/#26/#23/#25). Make the product safe and trustworthy; gate everything downstream. |
| **M1** | Integrate finished agent branches | #1, #2, #7, #8, #10 | Code largely exists — wiring + persistence, not greenfield. #7/#10 are quick; #8/#1 are user-priority and unlock M3. Banks visible wins cheaply. |
| **M2** | High-value UX & deploy-safety workflow | #14, #16, #18, #17, #20, #49, #54, #53, #50, #46 | Self-contained UX (#14/#16/#18/#17/#20) + the deploy-safety cluster (#49 dry-run → #54 restore → #53 verify → #50 Problems) which all reuse the same source-map vs `.lmmfiles` diff. #46 hooks wait on M0's command-escaping fix. |
| **M3** | Nexus / online depth | #9, #15, #27, #35, #47, #33, #29, #31 | Builds on M1 download plumbing + the #28 key fix. #9 pin (+ bug #41) gates #47 auto-checks; #15/#35 reuse fetched Nexus data; #29/#31 extend LootDeployer. |
| **M4** | Game-specific depth (TW3 / CP2077) | #56, #59, #57 | #56 auto-tags is pure data and underpins #57. #59 (vanilla scripts) raises the already-wired merger's quality. |
| **M5** | Large infrastructure | #12, #19, #13, #55, #24, #45, #44, #22, #11 | Cross-cutting, multi-subsystem. #19→#12→#24/#55; #13→#55; #45/#44 wrap mature `ApplicationManager` slots; #22 (i18n) last so strings have stabilized. |

**Dependency notes.** #28 before the M3 Nexus cluster & #13/#55 bundles (don't ship recoverable keys). #32 before #46 (hooks reuse the command path). #8 before #1/#33 (both drive downloads through the queue). #9+#41 before #47. #49 anchors #54/#53/#50 (shared diff). #56 underpins #57. #59 improves the wired #48 merger. #19 before #12/#24. #13 before #55. #29 before #50 (shared message surfacing).

**Quick wins (S):** #43, #42, #7, #10, #16, #18, #14, #20, #15, #23, #36, #26, #25, #56. **Large efforts (L):** #8, #1, #2, #33, #27, #11, #50, #45, #44, #24, #55, #22, #59, #31.

---

# Part 1 — Issues (fix first)

## Security

### Issue #28 — Nexus API key encrypted with a hardcoded default key
- **Goal:** Stop persisting a Nexus API key recoverable from config alone via a shipped constant; require a real secret or OS keystore.
- **Validity:** confirmed-real. `cryptography::default_key` (`cryptography.h:57`) is a compiled-in constant used as the fallback in `encrypt`/`decrypt` (`cryptography.cpp:42`, `:89`); the default path persists ciphertext + `info_is_default=true` to plaintext QSettings (`settingsdialog.cpp:269`) and auto-decrypts on startup (`mainwindow.cpp:1331-1356`). AES-GCM here is obfuscation only.
- **Approach:** (1) Minimum-viable: generate a per-install random 32-byte secret on first Nexus setup, store base64 in `<configdir>/nexus_secret.key` at 0600 via `QFile::setPermissions`, use it wherever `default_key` is substituted. (2) Lift the fallback OUT of the crypto layer so `encrypt/decrypt` never silently substitute; callers pass the resolved key. (3) Update the call sites (`changeapipwdialog.cpp:51-52`, `settingsdialog.cpp:347`/`:428`, `mainwindow.cpp:1333`). (4) Migration: on read, if `info_is_default` and no secret file, decrypt once with the old constant, re-encrypt with the new secret, rewrite; keep `default_key` transitional decrypt-only, then remove. (5) `chmod 0600` the QSettings `.conf` (`QSettings::fileName()`). (6) Stretch: QtKeychain (libsecret/KWallet), drop `info_c/n/t` from QSettings; gate behind a build option.
- **Touch points:** `cryptography.h:57`; `cryptography.cpp:42,89`; `settingsdialog.cpp:241-270,347,428`; `mainwindow.cpp:1331-1356`; `changeapipwdialog.cpp:51-52,84`; new nexus-secret util.
- **Considerations:** Migration is the crux — don't strand existing keys. All on UI/startup thread. `info_is_default` still distinguishes "user password" vs "machine secret" UX.
- **Effort:** M (per-install secret + migration + chmod); L for QtKeychain.
- **Risks:** QtKeychain optional on headless/Flatpak; secret-file location must be writable & outside synced dotfiles; confirm no other reader of `info_c/n/t`.
- **Depends on:** none.

### Issue #30 — FOMOD installer does not validate source paths (path-traversal read)
- **Goal:** Reject/normalize traversal in FOMOD `source` attributes so a malicious `ModuleConfig.xml` can't copy files from outside the extracted mod dir.
- **Validity:** confirmed-real. Destinations are guarded (`fomodinstaller.cpp:160-164`) but `source` is only `normalizePath`'d + `pathExists`'d (`:182-191`); `pathExists` returns the candidate unchanged when `base/candidate` exists (`pathutils.cpp:16-17`), so `../../home/<user>/.ssh/id_rsa` passes and `Installer::install` copies `tmp_dir / source_file` with no `..` guard (`installer.cpp:141,174-188`).
- **Approach:** (1) In `parseFileList`, apply the destination check to `source` too: `lexically_normal()` + reject `is_absolute()` or leading `..`; factor the existing check (`:160-164`) into a shared `pu::isContainedRelativePath`. (2) Defense-in-depth at the copy site: verify `weakly_canonical(tmp_dir / source_file)` is contained within `weakly_canonical(tmp_dir)`; throw + cleanup otherwise. (3) Prefer throw (matches existing behavior).
- **Touch points:** `fomodinstaller.cpp:182-191,160-165`; `pathutils.h/.cpp` (shared helper); `installer.cpp:141,174-188`.
- **Considerations:** Data-model only. `normalizePath` (backslash→slash) BEFORE `lexically_normal` so `..\..` is caught. Don't break legit nested sources (`data/textures/x.dds`). `weakly_canonical` containment also closes inward symlinks pointing out.
- **Effort:** S–M.
- **Risks:** Confirm no real FOMOD relies on a leading `..`; use `weakly_canonical` only on the source side where the file exists.
- **Depends on:** none (shares helper with the destination fix).

### Issue #32 — Tool/application commands not shell-escaped before popen()
- **Goal:** Prevent shell metacharacters in tool/app paths, args, env, and working dir from executing through `popen`.
- **Validity:** confirmed-real. `Tool::getCommand` (`tool.cpp:87-141`) wraps fields with `encloseInQuotes` (`tool.cpp:241-247`) which only adds bare double quotes and escapes nothing — inside `"…"` the shell still expands `$`, backticks, `\`, and a literal `"` breaks out. Flows to `popen` via `runCommand` (`mainwindow.cpp:1109`), reachable from tool launch (`:3801,3808`) and app launch (`:2619`). Fields load verbatim from per-app JSON (`tool.cpp:58-85`). Same flaw in `cyberpunkredmod.cpp:20-23` (`quote`). The #34 fix already added the correct pattern (`shellEscape`, `cyberpunkredmod.cpp:36-48`).
- **Approach:** (1) Replace `encloseInQuotes` with POSIX single-quote escaping identical to `cyberpunk_redmod::shellEscape` (`'`→`'\''`, wrap in `'…'`); drop the "already quoted" passthrough. (2) Apply to every interpolated untrusted field: `working_directory_` (`:109,111`), `executable_path_` (`:135`), env values (`:237`), and the currently-unescaped `arguments_` (`:138`) / `protontricks_arguments_` (`:130`) — decide tokenize-and-escape-each vs raw-fragment policy. (3) Replace `quote` in `cyberpunkredmod.cpp:20-23` with `shellEscape`. (4) Preferred deeper fix (L): switch the sink to `QProcess` with explicit program + `QStringList` argv (no shell) — non-trivial given the `cd`/env/`flatpak-spawn`/`protontricks-launch` composition.
- **Touch points:** `tool.cpp:241-247,109,111,130,135,138,237`; `cyberpunkredmod.cpp:20-23`; `mainwindow.cpp:1102-1122,2619,3801-3808`.
- **Considerations:** `command_overwrite_`/app `info.command` is an intentional user shell string — do NOT escape that whole string. `runCommand` runs in a `QtConcurrent` worker (no UI-thread concern). No JSON format change.
- **Effort:** M (escape all fields + cyberpunk dedup); L (QProcess argv).
- **Risks:** Argument-splitting policy may change behavior for multi-arg tools; single-quoting blocks intentional `$VAR` in argument fields — verify none rely on it.
- **Depends on:** mirrors #34 — hoist `shellEscape` to a shared util so `Tool` and `cyberpunk_redmod` share one impl.

### Issue #37 — LsPakExtractor trusts attacker-controlled counts/offsets (overflow / OOB)
- **Goal:** Validate all untrusted size/count/offset fields from a `.pak` before arithmetic, allocation, seeks, indexing.
- **Validity:** confirmed-real. `readFileList` (`lspakextractor.cpp:116-132`) computes `sizeof(LsPakFileListEntry)*num_files` (276 bytes packed, `lspakfilelistentry.h:14-28`) in 32-bit `unsigned int` — wraps for large `num_files`, can slip a small `uncompressed_size` past the `1<<30` cap in `extractData` (`:45`) while the loop bound (`:129`) uses the same wrapped product → `data.data()+i` reads past the buffer. Header/per-entry `offset`/sizes used in `seekg`/`read`/decompress (`:39-114,119-126`) without validation; `extractFile` indexes `file_list_[file_id]` unchecked (`:109-114`).
- **Approach:** (1) Capture real archive size (`sfs::file_size`) in `init` (`:15-37`); thread it for bounds checks. (2) `readFileList`: do the multiply in `uint64_t`; reject overflow/over-cap (derive from `header_->file_list_size` and/or a hard ceiling); validate `file_list_offset+8+compressed_size <= file_size`. (3) After decompression assert `data.size() == num_files*sizeof(entry)` before the `reinterpret_cast` loop; iterate with `size_t`. (4) `extractData` (`:39-99`): validate `offset+length<=file_size`, check `gcount()` short-read, keep the `1<<30` cap. (5) `extractFile`: range-check `file_id`. (6) Per-entry: validate the 48-bit `offset`/sizes against `file_size`.
- **Touch points:** `lspakextractor.cpp:116-132,39-99,109-114,15-37`; `lspakextractor.h:56-78` (add `file_size_`).
- **Considerations:** Pure core parsing; runs during BG3 plugin scans, so fail by throwing (callers expect `std::runtime_error`). The exact-length assert is the key guard for the `reinterpret_cast`.
- **Effort:** S–M.
- **Risks:** Choosing the cap (too low rejects legit large BG3 paks — derive from `file_list_size`); confirm `LZ4_decompress_safe_partial` bounds output; endianness assumption already little-endian.
- **Depends on:** none.

## Bugs

### Issue #41 — Pinned mods skipped entirely in update checks
- **Goal:** Make a pin suppress notifications only until a strictly-newer remote version appears, OR correct the docs to match the implemented full-suppression behavior.
- **Validity:** partly-valid. The impl/doc divergence is real (`moddedapplication.cpp:1556-1557,1569-1570` exclude pinned mods; `modlistmodel.cpp:297-298` also hard-returns `false`). But the documented "strictly newer than pinned version" is **not cleanly feasible**: the pipeline is timestamp-based (`remote_update_time` vs `install_time`), `Mod` stores no "latest remote version" string, and the only comparator (`MainWindow::versionIsLessOrEqual`, `mainwindow.cpp:1451`) is UI-private, numeric-only, and mishandles real Nexus version strings.
- **Recommendation:** **Correct the header doc to match full-suppression** (feature #9's intent is literally "suppress update notifications") — low-risk, internally consistent. Pursue the version route only if explicitly wanted.
- **Approach (recommended, S):** Edit the doxygen on `pinModVersion` (`moddedapplication.h:698-704`) to state pinning fully suppresses until unpinned; leave the predicates as-is.
- **Approach (alternative, L):** Drop `&& mod.pinned_version.empty()` (`:1556-1557,1569-1570`); add a `latest_remote_version` field to `Mod` (mod.h + toJson/ctor), populate in `performUpdateCheck` (`:2359-2360`) from `getNexusPage(...).mod.version`; add a robust core version comparator (do NOT reuse the UI one); update `modHasUpdate` (`modlistmodel.cpp:294-301`).
- **Touch points:** `moddedapplication.h:698-704` (recommended). Alt: `moddedapplication.cpp:1556-1557,1569-1570,2359-2360`; `modlistmodel.cpp:294-301`; `mod.h:46`/`mod.cpp`.
- **Effort:** S (doc) / L (comparison).
- **Depends on:** none. **Pairs with #9 and component 1 (version utility).**

### Issue #42 — Stale mod rules on uninstall
- **Goal:** Remove `ModRule`s referencing a mod when it's uninstalled so `checkModRules` stops emitting permanent spurious warnings.
- **Validity:** confirmed-real. `uninstallMods` (`moddedapplication.cpp:193-248`) cleans groups/deployers/`installed_mods_`/tags but never `mod_rules_`; `checkModRules` (`:2544-2566`) then warns forever with `getModName` returning `""`.
- **Approach:** In the per-mod loop after `installed_mods_.erase(mod_iter)` (`:225`) and alongside `tag.removeMod(mod_id)`, add `std::erase_if(mod_rules_, [mod_id](const ModRule& r){ return r.source_mod_id==mod_id || r.target_mod_id==mod_id; });`. Runs before the single `updateSettings(true)` (`:248`).
- **Touch points:** `moddedapplication.cpp:225-233`.
- **Considerations:** `mod_rules_` is app-global (matches uninstall semantics); `<algorithm>` already in use (`erase_if` at `:2525`).
- **Effort:** S.
- **Depends on:** none.

### Issue #43 — Cyberpunk progress node not advanced on skipped mod path
- **Goal:** Advance the progress node on the skip paths in `CyberpunkDeployer::deployFilesWithRemap` so the bar reaches 100%.
- **Validity:** confirmed-real. `cyberpunkdeployer.cpp:129` sets total to `dest_files.size()`, but the `continue` at `:134-135` (missing mod path) and a **second** at `:145` (no source mapping) both skip `(*progress_node)->advance()`. The TW3 equivalent advances on both (`tw3deployer.cpp:170-181`).
- **Approach:** Add `if(progress_node) (*progress_node)->advance();` before both `continue`s.
- **Touch points:** `cyberpunkdeployer.cpp:134-135,138-145`.
- **Effort:** S.
- **Depends on:** none.

## UX

### Issue #23 — Launch button silent failure on empty command
- **Goal:** Give visible feedback when an app/tool launch command is empty instead of silently logging and returning.
- **Validity:** confirmed-real. `onLaunchAppButtonClicked` (`mainwindow.cpp:2610-2620`) and `onLaunchToolButtonPressed` (`:3799-3809`) both `Log::error`+`return` with no UI surface.
- **Approach:** Replace the bare `return` with `QMessageBox::information` and/or `setStatusMessage` (`:1033`) naming the fix ("No launch command set for … — add one via Edit application/tool"); mirror in the tool path. Optional: disable the run action/launch button when no command is configured.
- **Touch points:** `mainwindow.cpp:2614-2618,3803-3807`; optional `:2036`.
- **Effort:** S.
- **Depends on:** none.

### Issue #25 — No empty-state guidance when no application exists
- **Goal:** Show onboarding guidance on first run / when no app is configured.
- **Validity:** confirmed-real. `initUiWithoutApps` (`mainwindow.cpp:1366-1393`) only toggles `setEnabled` + flips the app button's default action; no explanatory text.
- **Approach:** Add a centered rich-text `QLabel` placeholder over the central area ("No applications yet — click + (Add application) or Import from Steam"); show/hide it in `initUiWithoutApps`; optionally a CTA button bound to `add_app_action_` (`:1268`). Lower priority: empty mod-list/deployer-list placeholders.
- **Touch points:** `mainwindow.cpp:1366-1393` + setup/`mainwindow.h` (new `QLabel*`).
- **Effort:** M.
- **Risks:** Placement (central widget vs `app_tab_widget`) so it's visible while lists are disabled.
- **Depends on:** none.

### Issue #26 — Manage Mod Rules dialog shows numeric IDs
- **Goal:** Show the target mod's name (optionally with id) instead of the raw id.
- **Validity:** partly-valid — plumbing already done; only rendering is wrong. `setupDialog` already receives `all_mods` as `(int,QString)` (`managemodrulesdialog.cpp:22,33-37`) but `refreshTable` writes `QString::number(rule.target_mod_id)` (`:51-52`). The "dialog unreachable" caveat is **stale** (wired at `mainwindow.cpp` ~`:583`/`:3400-3401`).
- **Approach:** Build an `id→name` lookup in `setupDialog`; render `"<name> [<id>]"` in `refreshTable`, falling back to `"<id>"`.
- **Touch points:** `managemodrulesdialog.cpp:31-37,51-52`; `managemodrulesdialog.h` (lookup member).
- **Effort:** S.
- **Depends on:** none.

### Issue #36 — ConflictDetailDialog shows two blank lists for a no-conflict mod
- **Goal:** Show an explicit "no conflicts" message instead of two empty panes.
- **Validity:** confirmed-real. The ctor only ever `addItem`s (`conflictdetaildialog.cpp:16-43`); with no conflicts both lists stay empty under "wins (0)"/"loses (0)". Reachable via `onConflictDetails`→`onGetFileConflicts` (`mainwindow.cpp:3409-3418,1901-1910`) — "unreachable" caveat is stale.
- **Approach:** After populate + title updates (`:46-49`), if both lists empty insert a disabled, non-selectable "No conflicting files for this mod." row (or a combined label); optional `.ui` placeholder text on `wins_list`/`losses_list` (`:26,48`).
- **Touch points:** `conflictdetaildialog.cpp:45-49`; `conflictdetaildialog.ui:26,48`.
- **Considerations:** Lists are `sortingEnabled` — make the placeholder `Qt::NoItemFlags`.
- **Effort:** S.
- **Depends on:** none.

### Issue #38 — Manage Groups dialog: immediate commits, no confirmation, no Cancel
- **Goal:** Make the save model legible — visible feedback after Rename/Save Notes + a consistent button model — keeping the Dissolve confirmation.
- **Validity:** confirmed-real. Rename/Save-Notes emit immediately with no feedback (`managegroupsdialog.cpp:109-130`); button box is `Close`-only (`.ui:118-123`); Dissolve already confirms (`:146-173`). Reachable (`mainwindow.cpp:574,3421-3423`) — "unreachable" caveat is stale.
- **Approach (recommended, M):** After Rename/Save-Notes emit (`:120,:129`), show an inline "Saved" `QLabel` cleared on next edit (keep live-save explicit); keep Dissolve's Yes/No.
- **Approach (alternative, L):** Convert to Save/Apply+Cancel; buffer edits in the existing local vectors and emit only on accept — reorders signal timing (`mainwindow.cpp:746-760`) and `groupDissolved` is structural, complicating a clean Cancel.
- **Touch points:** `managegroupsdialog.cpp:120,129`; `managegroupsdialog.ui:118-123`; (alt) `mainwindow.cpp:746-760`.
- **Effort:** M (recommended) / L (deferred-commit).
- **Depends on:** none.

---

# Part 2 — Enhancements

## A. Integrate finished agent branches (M1)

### Issue #1 + #2 — Import/install & export Nexus Collections
- **Goal:** Paste a Nexus Collection URL/ID to download+install every mod in phase order (#1); export the current deployer load order as a `collection.json` (#2).
- **Existing code:** branch `worktree-agent-acdeca2b7f5612b30` @ `f6029d6`. Adds `src/core/collectionmanifest.{h,cpp}`, `src/core/collectioninstaller.{h,cpp}`, `nexus::Api::getCollection/extractCollectionSlug/resolveFileId` (v2 GraphQL), `ModdedApplication::installCollection/exportAsCollection`, `src/ui/importcollectiondialog.{h,cpp,ui}`.
- **Approach:** (1) **Take the files** (don't cherry-pick — pre-PR-199 base conflicts): copy the 4 core files + dialog, add to `CMakeLists.txt`. (2) **Rework `exportAsCollection` for the tree API** (the only load-order rework): replace the flat `for(const auto& [mod_id, enabled] : getLoadorder())` with the tree traversal (`static_pointer_cast<DeployerModInfo>(w.lock())`, skip `isSeparator`, `e->id`). (3) `installCollection` integrates as-is — it calls `installMod` (dev signature matches), which appends via `addModToDeployer`, so processing in phase order yields the load order for free. (4) Wire the `download_callback` through `ApplicationManager::performDownload`; add an `installCollection(app_id,url,deployers,include_optional)` slot + `collectionInstalled(Summary)` signal. (5) UI: "Import Collection…" + "Export as Collection" actions in `mainwindow.ui`; register `ImportModInfo`/`Summary` metatypes.
- **Touch points:** `CMakeLists.txt:156`; `moddedapplication.cpp` (`exportAsCollection`/`installCollection`) + `.h:650,669`; `nexus/api.cpp`/`api.h:167-200`; `applicationmanager.{h,cpp}`; `mainwindow.{cpp,h,ui}`.
- **Considerations:** ~14 `// TODO(collections):` are **unvalidated network assumptions** (GraphQL host/query/auth from `nexus-collection-dl`; slug format; manifest field locations; **GraphQL omits install phases** so entries default to `phase=0` → ordering collapses to GraphQL order). FOMOD choices aren't replayed. Premium/NXM vs manual-download fallback unimplemented.
- **Effort:** L.
- **Risks:** Heavy reliance on undocumented Nexus collection GraphQL — needs live-API iteration; phase data loss; premium-only URLs.
- **Depends on:** synergy with #8 (manual-download queue).

### Issue #7 — Export deployer mod list as CSV / Markdown
- **Goal:** Export the current deployer's ordered mod list (name, version, enabled, URL, tags) to a timestamped CSV/Markdown file.
- **Existing code:** commit `351bc26`. Adds `src/core/exportformat.h` + `ModdedApplication::exportModList(int, ExportFormat)` (`moddedapplication.cpp` ~1535-1685). No UI.
- **Approach:** (1) Take the files; add `exportformat.h` to CMake. (2) **Rework the two flat-API loops** (`~:1612` CSV, `~:1655` MD): replace `for(const auto& [mod_id,enabled] : loadorder)` with the tree traversal, use `e->id`/`e->enabled`. (3) Everything else ports unchanged. (4) UI: `ApplicationManager::exportModList` slot + `MainWindow` signal + a toolbar/menu action with a 2-entry format chooser; `Q_DECLARE_METATYPE(ExportFormat)`.
- **Touch points:** `CMakeLists.txt`; `moddedapplication.cpp:~1612,~1655` + `.h:640`; `applicationmanager.{h,cpp}`; `mainwindow.{cpp,h,ui}`.
- **Effort:** S.
- **Depends on:** none.

### Issue #8 — Persistent download queue with progress and retry
- **Goal:** Persist the queue to `_download/lmm_queue.json`, restore incomplete downloads on startup, add a Downloads panel with per-entry progress/status/retry.
- **Existing code:** branch `feat/issue-8-download-queue-persistence` @ `28e7fba`. Adds `downloadqueuemodel.{h,cpp}`, `downloadqueuepersistence.{h,cpp}`, new `ApplicationManager` signals/slot, `MainWindow` plumbing (`download_queue_model_`, `retryDownload`, …).
- **Approach:** (1) Take the 4 UI files (no tree rework — they touch the download path); add to CMake. (2) Re-apply `ApplicationManager` deltas by hand: `downloadProgress` emit in `performDownload`'s lambda, `downloadStarted` in `downloadMod`, new `getDownloadDir`/`sendDownloadDir`. (3) Re-apply `MainWindow` deltas, reconciling the **multiple enqueue sites** on dev (`mainwindow.cpp:148,1557,1588,2205`). (4) **Build the Downloads panel — the real gap** (the branch adds no `.ui`): new tab in `app_tab_widget` with a `QTableView` + progress-bar delegate + Retry; `markActiveAsFailed()` on shutdown.
- **Touch points:** `CMakeLists.txt:~281`; `applicationmanager.cpp:49,1062-1105` + `.h:498-519`; `mainwindow.cpp` enqueue sites + connects; `mainwindow.ui` (new tab — net-new).
- **Considerations:** Persistence omits the `priority_queue` ordering (`queue_time` defaults to now on restore — persist it or accept reordering). `onDownloadStarted` matches by `remote_request_url`/`remote_source+file_id` (fragile). `ImportModInfo` metatype already registered.
- **Effort:** L (model/persistence done; the panel UI + reconciling enqueue sites is the work).
- **Depends on:** none (synergizes with #1/#2).

### Issue #10 — Add/rename/delete deployer separators from the UI
- **Goal:** Full separator UI: insert named at current position, rename/delete via context menu (delete promotes children), persist collapsed state.
- **Existing code:** commit `0c149c2` — **obsolete OLD design** (flat-vector sentinel id `SEPARATOR_ID_BASE=-1000`), superseded by PR #199's inline `TreeItem<DeployerEntry>` separators. **Do not integrate the branch backend.** It has no MainWindow changes, so nothing for the actual gap.
- **Approach (plan only the gap on the tree API):** Already done on dev (no work): inline tree separators persist (`deployer.cpp:96-110`), render bold/expandable (`deployerlistmodel.cpp:140-149`), **inline rename works** (`flags()` editable + `setData`, `:244,268`), **"Add Separator" button exists**, collapse toggling (`deployerlistview.cpp:142`), **delete-promotes-children** via `TreeItem::remove` (`treeitem.t.hpp:165-185`). **Gap 1 — insert at position:** rework `DeployerListModel::addSeparator()` (`:250-259`) to `TreeItem::insert(position, item)` at the selected row, with `begin/endInsertRows`; pass the selection from the toolbar handler. **Gap 2 — context-menu Rename/Delete:** add 2 `QAction`s to `deployer_list_menu_` (`mainwindow.cpp:592-621`) gated on `is_separator` (`:1839`); Rename → `edit(index)`; Delete → reuse `removeNodeFromDeployer` (`:2550`). **Gap 3:** prompt for a name on insert (currently hard-codes "Separator"). Every action → `commitChanges` + `getDeployerInfo`.
- **Touch points:** `deployerlistmodel.cpp:250` + `.h:104`; `mainwindow.cpp:592-621,1839,2412,2550`; `mainwindow.{h,ui}`. (No core changes.)
- **Effort:** S–M.
- **Depends on:** none (PR #199 already merged).

## B. Workflow / UX / infra

### Issue #11 — Virtual deployed-file tree with per-file origin
- **Goal:** Read-only tree of the final merged deployment for the current deployer, nodes labeled with winning mod + priority.
- **What exists:** `Deployer::getConflictGroups()` (`deployer.h:225`), `getModFiles` (`:479`), `getModNames` (`:248`), `getLoadorder` (`:107`); `DeployerInfo` ships to UI; `conflictinfo.h`/`colors.h`; `TreeItem<T>` (`treeitem.h`).
- **Approach:** Add `Deployer::getDeployedFileTree()` (path→{mod_id,priority,is_conflict}, last-writer-wins); surface via a new `ApplicationManager` signal/slot (mirror `getDeployerInfo`); build `QTreeView` + `DeployedFileTreeModel` in a new "File Tree" tab (`mainwindow.ui:442`); color/icon conflicts; refresh on `completedOperations` + profile switch.
- **Touch points:** `deployer.h:225,479,248`; `applicationmanager.h:577`; new `deployedfiletreemodel.{h,cpp}`; `mainwindow.ui:442`; `mainwindow.cpp:~2311`.
- **Considerations:** Build on worker thread; send a serializable structure (not live `Deployer*`); cache per deployer/profile.
- **Effort:** L.
- **Risks:** Subclass deployers may not enumerate via `getModFiles`; large modlists expensive.
- **Depends on:** none.

### Issue #12 — Per-profile game INI/config management
- **Goal:** Each profile gets its own snapshot of declared game config files, swapped atomically on profile switch.
- **What exists:** `setProfile/addProfile/removeProfile` fan out to deployers + `bak_man_` (`moddedapplication.cpp:606,616,628`); `BackupManager` already does per-profile snapshot+swap; `path_utils::copyOrMoveFiles`; token expansion `$STEAM_PREFIX_PATH$`/`$HOME$` (`addappdialog.cpp:187,221`).
- **Approach:** Add `managed_config_paths_` to `ModdedApplication` (serialize in `updateSettings`/`updateState`); `profile_configs/<profile>/` under staging; in `addProfile` copy current files; in `setProfile` copy the snapshot onto live paths (capture pre-Limo originals for reset); managed-path editing in the dialog; defaults from `steam_app_configs/*.json`.
- **Touch points:** `moddedapplication.cpp:606,616,1743,2070` + `.h:792-806`; `pathutils.cpp`; `addprofiledialog.*`/`addappdialog.cpp:155+`; `steam_app_configs/*.json`.
- **Considerations:** Atomicity (temp-then-rename); capture-on-switch-out so external edits aren't lost.
- **Effort:** M.
- **Depends on:** #19 (Proton path defaults).

### Issue #13 — Profile export/import as zip bundle
- **Goal:** Export a profile's metadata (order, enabled, groups, tags, notes, optional config snapshots) to `.limo-profile.zip`; import to recreate.
- **What exists:** `exportConfiguration` (`moddedapplication.h:666`) serializes a curated subset; `Installer::extract` as unzip template; libarchive linked.
- **Approach:** Add `exportProfile(profile, path, include_configs)` (mods_list.json + deployers.json + optional configs, zip via libarchive write API); `importProfile(path)` (extract → `addProfile` → apply order/enabled/tags, return missing-mods list with Nexus IDs); wire as profile menu actions through `ApplicationManager`; reuse `ExportAppConfigDialog` pattern.
- **Touch points:** `moddedapplication.cpp:666,1743` + new methods; `installer.cpp` (unzip ref); `applicationmanager.h`; `mainwindow.cpp:~926`; optional new dialog.
- **Considerations:** Metadata only (no staging payload); JSON schema/version field; no libarchive *write* helper yet (new).
- **Effort:** M.
- **Depends on:** #6 (notes — already landed), optionally #12.

### Issue #14 — Bulk enable/disable/tag on selected mods
- **Goal:** Apply enable/disable/tag/move/delete to a multi-selection in one batched single-deploy op.
- **What exists:** Multi-select already wired (`modlistview.cpp:54-84,158,171`); `onModListContextMenu` already branches on >1 (`mainwindow.cpp:1777-1803`); `updateModDeployers`/`uninstallMods` take vectors; `actionSelect_All` exists.
- **Approach:** Add Enable-all/Disable-all/Add-tag/Remove-tag to the >1 branch (reuse `ManageModTagsDialog`); add a batched `setModsStatus(app,deployer,ids,status)` that loops then deploys once; wire `actionSelect_All`→`selectAll()` respecting the proxy; mirror into the deployer list.
- **Touch points:** `mainwindow.cpp:1777-1803,99,577`; `applicationmanager.h:638,612,621`; `moddedapplication.cpp` (batched loop); `deployerlistview.cpp:140`.
- **Considerations:** Defer a single `deployMods` to batch end (the core requirement); selection is in proxy coords.
- **Effort:** S.
- **Risks:** No undo stack — scope to "single deploy", not true undo.
- **Depends on:** none.

### Issue #16 — Drag-and-drop archive install from file manager
- **Goal:** Dropping archives/dirs onto the mod list opens the Add-mod flow pre-populated, queuing multiples.
- **What exists:** `ModListView::dropEvent` already emits `modAdded(urls())` (`modlistview.cpp:21-25`); drag enter/move overridden (`:28,38`); `AddModDialog` accepts a preset path.
- **Approach:** Connect `ModListView::modAdded` in `mainwindow.cpp` to a handler opening `AddModDialog` per URL (sequential queue); in `dragEnterEvent` accept only supported-extension URLs (keep internal reorder working).
- **Touch points:** `modlistview.cpp:21-48`; `mainwindow.cpp:~554`; `addmoddialog.*`.
- **Considerations:** Distinguish internal-reorder MIME from external URLs; sequential dialog queue.
- **Effort:** S.
- **Risks:** Confirm `modAdded` isn't already connected (avoid double-handling).
- **Depends on:** none.

### Issue #17 — Theme/color-scheme selector
- **Goal:** Settings selector to force System/Light/Dark/High-contrast independent of the desktop, applied live + persisted.
- **What exists:** Startup loads `:/styles/app.qss` (`main.cpp:26-28`); `SettingsDialog` persists to QSettings (`settingsdialog.cpp:38,116`); `colors.h` centralizes palette.
- **Approach:** Add an "Appearance" combo + persist `color_scheme`; `applyColorScheme()` helper (from `main.cpp` + `onSettingsDialogComplete`) sets palette/`Qt::ColorScheme` + swaps QSS; author light/dark QSS in qrc; refresh `colors.h`-derived brushes.
- **Touch points:** `main.cpp:25-28`; `settingsdialog.ui`/`.cpp:38,116`; `mainwindow.cpp` (`onSettingsDialogComplete`); `styles/*.qss` + qrc; `colors.h`.
- **Considerations:** Live re-apply (re-set stylesheet/palette on all top-levels); `colors.h` are compile-time constants — may need indirection.
- **Effort:** M.
- **Depends on:** none (uses component 5).

### Issue #18 — Install-date & size sortable columns
- **Goal:** Make Installed/Size columns toggleable + all columns header-sortable, per-app persisted visibility.
- **What exists:** Mostly built. `time_col`/`size_col` exist (`modlistmodel.h:38,40`), rendered (`modlistmodel.cpp:89-96,249-269`); proxy already sorts by size (`modlistproxymodel.cpp:168-173`); sort persisted (`mainwindow.cpp:486,1221-1228`).
- **Approach:** Add a `time_col` branch to `lessThan` (sort by raw `install_time`, **fixing a latent lexicographic bug**); header context menu with checkable show/hide; persist visibility per app.
- **Touch points:** `modlistproxymodel.cpp:165-174`; `mainwindow.cpp:486,1221-1228`; `modlistmodel.cpp:89-96`.
- **Effort:** S.
- **Depends on:** none.

### Issue #19 — Proton-prefix auto-detection
- **Goal:** Auto-resolve the Proton prefix for Steam-imported games + expand a `${PROTON_PREFIX}` variable for config/tool/save paths.
- **What exists:** `ImportFromSteamDialog` already detects `compatdata/<appid>` → `prefix_path` (`importfromsteamdialog.cpp:165-227`); `AddAppDialog` expands `$STEAM_PREFIX_PATH$`/`$HOME$` (`addappdialog.cpp:187,221,295`); `steam_app_id_` persisted.
- **Approach:** `path_utils` resolver from `steam_app_id` (scan `libraryfolders.vdf` → `compatdata/<appid>/pfx`); persist a `${PROTON_PREFIX}` token + runtime expander; extend `steam_app_configs/*.json` with `proton_config_paths` (add TW3 `.settings`, CP2077 `UserSettings.json`); feed #12/#24; no-op when absent.
- **Touch points:** `pathutils.h/.cpp`; `addappdialog.cpp:187,221`; `importfromsteamdialog.cpp:220-227`; `steam_app_configs/{377160,1091500,292030}.json`.
- **Effort:** M.
- **Depends on:** none (enables #12, #24).

### Issue #20 — In-app log viewer panel
- **Goal:** Finish the bottom log panel with level filtering + copy/clear.
- **What exists:** Largely built. `Log::log_printers` append color-coded HTML, auto-show on error/warning (`mainwindow.cpp:1041-1092`); `log_container`/`log_frame`/`tool_log_frame` in UI; status-bar "Log" toggle; `setMaximumBlockCount(1000)`; `onReceiveLogMessage` bridges threads.
- **Approach:** Add INFO/WARNING/ERROR toggles (track each line's level in a side buffer); Copy-all (`QClipboard`) + Clear (widget only, not the file); make the views read-only; ensure printer callbacks marshal via the queued bridge.
- **Touch points:** `mainwindow.ui:1184-1256`; `mainwindow.cpp:1041-1099,2836-2843`; `colors.h`.
- **Effort:** S.
- **Depends on:** none (uses component 3).

### Issue #24 — Save-game manager tab
- **Goal:** A "Saves" tab listing the active app/profile's saves with metadata + open/delete/rename/backup-restore, optionally profile-linked.
- **What exists:** No save handling today. Strong templates: `BackupManager` (per-profile snapshot/swap), `BackupTarget`, the `backuplistmodel/view/delegate` trio, the `ApplicationManager` worker pattern, `path_utils` + #19's resolver.
- **Approach:** Add a `SaveManager` core class mirroring `backupmanager.{h,cpp}` (enumerate with light per-game parsers: Bethesda `.ess`/`.fos`, TW3 `.sav`); auto-locate saves via #19; persist `saves_path` per profile; add a Saves tab + `SaveListModel`/`SaveListView`; optional swap-on-profile-switch.
- **Touch points:** new `savemanager.{h,cpp}`, `savelistmodel.*`/`savelistview.*`; `applicationmanager.h`; `mainwindow.ui` (new tab); `moddedapplication.cpp:606,1743`.
- **Considerations:** Save-format parsers per-game and brittle (name/timestamp/size only); IO on worker thread; reuse backup machinery.
- **Effort:** L.
- **Depends on:** #19.

## C. Nexus / online + i18n

### Issue #15 — In-app Nexus mod changelog viewer
- **Goal:** Surface a mod's changelog on the update badge tooltip + a dedicated dialog tab.
- **What exists:** Mostly built — `nexus::Page::changelog` (`api.h:31`), fetched by `getChangelogs`/`getNexusPage` (`api.cpp:207,296`); the dialog already renders it (`nexusmoddialog.cpp:37-46`; `.ui:80`).
- **Approach:** Verify the dialog's empty-state ("No changelog available."); for the tooltip AC, persist a short changelog snippet with mod metadata (changelog not currently stored — uses #35's persistence plumbing); extend `ModListModel` `Qt::ToolTipRole` (`:71`) when `has_update`; refresh the cache during update checks.
- **Touch points:** `nexusmoddialog.cpp:37-46`; `modlistmodel.cpp:71,299`; `mod.cpp:39-85`; `moddedapplication.cpp:1551-1570`.
- **Effort:** S (dialog) / M (cached-tooltip persistence).
- **Depends on:** #35 (shared persistence), #47 (refresh trigger).

### Issue #22 — Multi-language UI (Qt i18n)
- **Goal:** Translatable UI via `QTranslator` + a Language selector, shipping `.ts`/`.qm`.
- **What exists:** Nothing — no `.ts`/`.qm`, no `QTranslator` in `main.cpp`, no LinguistTools in CMake. `.ui` strings are translatable; ~223 `tr()` calls already exist.
- **Approach:** Add `find_package(Qt6 COMPONENTS LinguistTools)` + `qt_add_translations`; in `main.cpp` load locale from QSettings + `installTranslator`; audit hard-coded UI literals (e.g. context-menu strings `mainwindow.cpp:560-611`) and wrap user-facing ones in `tr()`; add a Language combo to settings + restart prompt.
- **Touch points:** `CMakeLists.txt:36`; `main.cpp:24,126`; `settingsdialog.cpp:38,122` + `.ui`; `mainwindow.cpp:560-611`.
- **Considerations:** Restart-on-change acceptable v1 (retranslate-on-the-fly is heavy); `.qm` install location for flatpak.
- **Effort:** L (broad string audit).
- **Depends on:** none (do last so strings stabilize).

### Issue #27 — Endorse and track mods on NexusMods
- **Goal:** Endorse/abstain + track/untrack installed Nexus mods from the list/dialog, show state, + a tracked-mods view.
- **What exists:** `Api::trackMod`/`untrackMod`/`getTrackedMods` exist but unused (`api.cpp:44,56,68`); **no** endorse call. `Mod` parses `endorsement_status`/`count` (`nexus/mod.h:66,90`).
- **Approach:** Add `endorseMod(url,bool)` to `nexus::Api` (POST `…/endorse.json`/`abstain.json`; check `status_code`); worker slots `endorseMod`/`setModTracked` + result signals; context actions ("Endorse", "Track/Untrack"); show state in `nexusmoddialog`; a "My tracked mods" view via `getTrackedMods`.
- **Touch points:** `nexus/api.cpp:44`; `applicationmanager.cpp:1051`; `mainwindow.cpp:575,3512`; `nexusmoddialog.cpp:29,192`.
- **Considerations:** Needs valid API key; all network on worker; endorse may require a version arg + prior download.
- **Effort:** M.
- **Depends on:** #33 (shared tracked-mods view).

### Issue #29 — Surface LOOT plugin messages and warnings
- **Goal:** Show LOOT per-plugin diagnostics (missing masters, dirty, incompatibilities, notes) as badges + a details panel.
- **What exists:** `sortModsByConflicts` already inspects metadata but only logs (`lootdeployer.cpp:243-256`); `DeployerInfo` carries no message field; `DeployerListModel::data` handles roles/tooltips (`deployerlistmodel.cpp:58-80`).
- **Approach:** While the handle is open, collect `GetGeneralMessages()`/evaluated `GetMessages()`/cleaning data into a per-plugin vector; add `plugin_messages` to `DeployerInfo`; badge via `Qt::DecorationRole` + tooltip; a "LOOT Messages…" popup; recompute on load + sort.
- **Touch points:** `lootdeployer.cpp:182,205,243-256`; `deployerinfo.h:17`; `deployerlistmodel.cpp:58-80`; `mainwindow.cpp` (Sort path).
- **Considerations:** Worker thread; new `DeployerInfo` field must be copyable/registered; guard with `LIMO_WITH_LOOT`.
- **Effort:** M.
- **Depends on:** #31 (shared LOOT handle path + messages popup).

### Issue #31 — LOOT user-metadata editor (userlist.yaml)
- **Goal:** Dialog to assign LOOT groups, add load-after/requires rules, mark clean/dirty per plugin → persist to `userlist.yaml` + re-sort.
- **What exists:** `LootDeployer` only *loads* userlist (`lootdeployer.cpp:183-191`); no write path. Sort re-run hook is `sortModsByConflicts`→`SortPlugins` (`:160,205`). `managemodrulesdialog` is the structural precedent (distinct — that writes Limo's own rules).
- **Approach:** Add core methods to read/modify `PluginMetadata` + `WriteUserMetadata(path, overwrite=true)`; expose via `ModdedApplication` + worker slots; new dialog (group combo from masterlist+user groups, load-after/requires + clean/dirty); re-run sort after save; context-menu entry.
- **Touch points:** `lootdeployer.cpp:160,182-191,205` + `.h`; `moddedapplication.cpp`; `applicationmanager.*`; new `lootmetadatadialog.*`; `mainwindow.cpp:612`.
- **Considerations:** Worker thread; `LIMO_WITH_LOOT`; persistence is `userlist.yaml`; only write the user layer.
- **Effort:** L.
- **Depends on:** #29 (shared LOOT plumbing).

### Issue #33 — In-app NexusMods browsing and search
- **Goal:** An in-app browser to search the active game's Nexus mods, view, and install via the existing pipeline.
- **What exists:** `nexus::Api` fetches only specific mods — **no search/listing**. `Mod` has `picture_url`/category. Install pipeline exists (`ImportModInfo`, `downloadMod`, import queue).
- **Approach:** Add search/trending/latest to `nexus::Api` (public mods-by-category/updated/trending; full keyword search may need scraping); worker slots returning `std::vector<nexus::Mod>`; a browse panel (search box, sort/category, thumbnails via #35); open `NexusModDialog` + route install through the queue; scope to the app's Nexus domain.
- **Touch points:** `nexus/api.cpp:30,296`; `applicationmanager.cpp:1051,1062`; new `nexusbrowserdialog.*`; `mainwindow.cpp`.
- **Considerations:** API key + rate limits; per-app domain resolution.
- **Effort:** L.
- **Risks:** No official full-text search endpoint — likely HTML scraping (fragile).
- **Depends on:** #35 (thumbnails), #27 (shared tracked view).

### Issue #35 — Nexus preview thumbnails
- **Goal:** Per-mod Nexus preview as a toggleable list thumbnail + larger image in the info dialog, fetched lazily, disk-cached, placeholder fallback.
- **What exists:** `nexus::Mod` parses `picture_url` but core `Mod` doesn't store it (`mod.cpp:39-85`); `ModListModel` only sets `icon_role` for the note; **no `QNetworkAccessManager`** anywhere (core nets via cpr).
- **Approach:** Persist `picture_url` in core `Mod` (+ toJson/fromJson), capture during update checks/imports; add a UI-layer async image fetcher (`QNetworkAccessManager`) + on-disk cache; settings-gated thumbnail column / `Qt::DecorationRole` with placeholder; full image in `nexusmoddialog`; settings toggle.
- **Touch points:** `mod.cpp:39-85` + `mod.h`; new `imagecache.*`; `modlistmodel.{h,cpp}:30,52,65`; `nexusmoddialog.cpp:29`; `settingsdialog.cpp:122`.
- **Considerations:** `QNetworkAccessManager` on UI thread (distinct from cpr); `dataChanged` on pixmap arrival; **adding a column shifts all `*_col` indices** — audit proxy/sort/delegates.
- **Effort:** M.
- **Depends on:** none (enables #33).

### Issue #47 — Scheduled / startup automatic update checks
- **Goal:** Opt-in update checks on startup and/or every N hours, non-blocking badge, rate-limited, key-gated.
- **What exists:** Update logic fully wired (`checkForModUpdates`/`checkModsForUpdates`, already skip pinned + respect suppress); only trigger today is the manual button (`mainwindow.cpp:3623`); badge via `has_update_role`. QTimer precedent at `deployerlistview.cpp:26-28`.
- **Approach:** Add a `QTimer` started after startup; on timeout, if a key is configured + toggle on, emit `checkForModUpdates` (+ `getModInfo`) without the busy UI; one-shot check after startup; settings toggle + interval; reuse the badge; throttle + single-flight.
- **Touch points:** `mainwindow.{h,cpp}:3623`; `settingsdialog.cpp:122` + `.ui`; `main.cpp:130`; `moddedapplication.cpp:1551`.
- **Effort:** S–M.
- **Depends on:** #15 (changelog refresh can piggyback), #9/#41 (honor pins), component 5 (settings).

## D. CLI / diagnostics / game-specific

### Issue #44 — Headless CLI: install/uninstall/profile/status
- **Goal:** Extend the GUI-less CLI from `--list/--deploy/--profile` into a scriptable surface (install, uninstall, profile-switch, enable/disable, JSON status).
- **What exists:** `main.cpp:32-99` already builds an `ApplicationManager` with `enableExceptions(true)`, no event loop; `--list` prints `toString()`; `getNumApplications`/`getNumProfiles` exist.
- **Approach:** Add `QCommandLineOption`s (`--install`/`--uninstall`/`--set-profile`/`--enable`/`--disable`/`--status`/`--json`/`--app`/`--deployer`); branch like `--deploy`, validate ids, reach `ModdedApplication` synchronously (add sync accessors); `--install` builds `ImportModInfo`; `--status --json` serializes `ModInfo`/`DeployerInfo` to stdout.
- **Touch points:** `main.cpp:32-99`; `applicationmanager.h:50-74` (sync accessors / `toJson`); `moddedapplication.h:77,84,118,248`.
- **Considerations:** No `QApplication` event loop for headless; keep the single-instance IPC check; non-interactive FOMOD needs an answer-file (new design).
- **Effort:** M.
- **Risks:** Non-interactive FOMOD; concurrency if a GUI holds the same `lmm_apps.json`.
- **Depends on:** loosely #45 (importer plumbing), #55.

### Issue #45 — Import MO2 / Vortex setup
- **Goal:** Read an existing MO2 instance / Vortex deployment and recreate it as a Limo app/profile (mods, order, enabled state).
- **What exists:** None. `ImportModInfo` models a pre-extracted mod; `installMod` consumes it; `Deployer::setLoadorder(Json::Value)` reconstructs order+enabled; `Plugin/LootDeployer` already parse `plugins.txt`/`loadorder.txt`.
- **Approach:** New `Mo2Importer`/`VortexImporter` in core (MO2: `profiles/<name>/modlist.txt` + `mods/<name>/`; Vortex: `state.v2` + staging manifest); register existing dirs without re-extraction via `ImportModInfo.current_path`; translate to the load-order JSON `setLoadorder` ingests; new `ImportInstanceDialog`; flag mods with no archive source.
- **Touch points:** new `mo2importer.{h,cpp}`/`vorteximporter`; `importmodinfo.h:16`; `deployer.cpp` (setLoadorder); new `importinstancedialog.*`; wire like add-app.
- **Considerations:** MO2 `modlist.txt` is highest-priority-first (invert); Windows path case; copy vs reference the foreign `mods/` tree.
- **Effort:** L.
- **Risks:** Vortex `state.v2` is LevelDB/opaque — parsing is the main unknown.
- **Depends on:** none.

### Issue #46 — Pre/post-deploy hooks/scripts
- **Goal:** Bind commands/scripts to the deploy lifecycle (pre/post deploy/undeploy), per-app/per-deployer, with env + fail-on-nonzero.
- **What exists:** `Tool` already builds native/wine/protontricks/steam commands (`getCommand`) + serializes; tools persisted in `lmm_apps.json`; deploy entrypoints `deployMods`/`deployModsFor` (`moddedapplication.cpp:43,51`). Tools only run on manual click today.
- **Approach:** Add a `trigger` enum + `fail_on_error` + `deployer_scope` to `Tool` (+ toJson/ctor); in the deploy/undeploy loop iterate `tools_`, run matching-trigger tools before/after `deploy(...)` through the hardened command path (after #32); abort + surface on non-zero when `fail_on_error`; expose fields in `AddToolDialog`; run hooks synchronously on the worker thread.
- **Touch points:** `tool.{h,cpp}`; `moddedapplication.cpp:43-127`; `addtooldialog.*`, `edittoolwidget.*`.
- **Considerations:** Must use the escaped command path; ordering across deployers; migrate old tool JSON without `trigger`.
- **Effort:** M.
- **Depends on:** **#32 (command escaping) — required.**

### Issue #49 — Deploy dry-run / preview
- **Goal:** "Preview deploy" computing added/overwritten(+winner)/removed/orphaned files without mutating, then confirm.
- **What exists:** `Deployer::deploy` (`deployer.cpp:45-64`) separates computation from mutation: `getDeploymentSourceFilesAndModSizes` + `loadDeployedFiles` (`.lmmfiles`) build state; only `backup/deploy/save` mutate. Dry-run flag precedent in `Tw3MergeUtil`.
- **Approach:** Add `Deployer::previewDeploy(loadorder)` diffing source map vs `loadDeployedFiles` into {added, overwritten+winner, removed, orphaned} with no writes; aggregate in `ModdedApplication::previewDeploy()`; `ApplicationManager` slot/signal; confirmation dialog before `on_deploy_button_clicked`.
- **Touch points:** `deployer.{h,cpp}:45,489,607`; `moddedapplication.cpp:43-100`; `applicationmanager.{h,cpp}`; `mainwindow.cpp:2402` + new dialog.
- **Considerations:** Winner = last in loadorder; **Tw3/Cyberpunk override `deploy` with path remapping** — preview must replicate that remap or be inaccurate.
- **Effort:** M.
- **Depends on:** feeds #50; pairs with #54; **component 7 (deploy transaction).**

### Issue #50 — 'Problems' / health-check panel
- **Goal:** Aggregate diagnostics: orphaned files, broken hardlinks, unmet rules, conflict overview, unwritable/fallback deployers — each with a fix action.
- **What exists:** Scattered: `checkModRules()` warning string; `getExternalChanges`→`getExternallyModifiedFiles` (broken links, `deployer.cpp:833-870`); `verifyDeployerDirectories()` + `fixInvalidHardLinkDeployers()` (`moddedapplication.cpp:663,~690`); `getModConflicts`/`getFileConflicts`; orphans = the #49 diff.
- **Approach:** Define `struct Problem{type,severity,message,deployer_id,mod_id,fix_action}`; `runHealthCheck()->vector<Problem>` composing the above; `ApplicationManager` slot/signal; new `ProblemsPanel` dock, rows dispatching re-deploy/remove-orphan/jump-to-mod.
- **Touch points:** `moddedapplication.h`/`.cpp:663` + `getExternalChanges`/`checkModRules`; `applicationmanager.{h,cpp}`; new `problemspanel.*` + `mainwindow.cpp`.
- **Considerations:** Scan-heavy → worker thread + progress; orphan vs intentional external file; conflict overview not O(n²).
- **Effort:** L.
- **Depends on:** consumes #49 (orphan diff) + #53 (integrity rows); #29 (shared message surfacing); components 2/3/7.

### Issue #53 — Deployment integrity verification
- **Goal:** "Verify deployment" auditing live target files vs `.lmmfiles` (existence, correct link/copy, not truncated/replaced), optional hashes + one-click repair.
- **What exists:** `save/loadDeployedFiles` (`.lmmfiles`); `getDeploymentSourceFilesAndModSizes`; `getExternallyModifiedFiles` (`deployer.cpp:833`) already compares hard-link equivalence/symlink targets — but only flags *modified existing*, not *missing*/*unexpected*.
- **Approach:** `struct DeploymentReport{missing,mismatched,unexpected}` + `Deployer::verifyDeployment()` generalizing `getExternallyModifiedFiles` (existence + still-linked/identical + optional SHA vs a stored manifest + unexpected scan); optional hash manifest; repair = targeted re-deploy (`updateDeployedFilesForMod`); menu action + slot/signal; feed #50.
- **Touch points:** `deployer.{h,cpp}:833,914`; `moddedapplication.{h,cpp}`; `applicationmanager.*`; `mainwindow.cpp`.
- **Considerations:** copy-mode can't use link equivalence (size/hash); hashing large dirs slow → opt-in; "unexpected" includes other deployers' output + base game.
- **Effort:** M.
- **Depends on:** feeds #50; overlaps #49; component 7.

### Issue #54 — Deploy restore points / undo
- **Goal:** Auto-snapshot each deployer's load order + enabled/group state before each deploy; one-click rollback (restore + redeploy); keep-last-N + pin.
- **What exists:** Load order/state fully JSON-serialized (`setLoadorder(Json::Value)`); `BackupManager` versions *file targets*, not load order (not reusable here).
- **Approach:** Before mutation in `deployModsFor`, serialize each deployer's `getLoadorder()` subtree + group state to `restore_points/<timestamp>.json`; rolling N + pin; `createRestorePoint()/listRestorePoints()/applyRestorePoint(id)` (apply via `setLoadorder` + redeploy); new dialog + slots.
- **Touch points:** `moddedapplication.cpp:43-100` + new methods/`.h`; `deployer.cpp:92-130`; `applicationmanager.*`; new `restorepointsdialog.*` + `mainwindow.cpp`.
- **Considerations:** Config-only snapshot (tiny); scope (all profiles vs active); pruning + pinned exemption; restoring after mods uninstalled (dangling ids).
- **Effort:** M.
- **Depends on:** pairs with #49; component 7.

### Issue #55 — Portable/relocatable instances + export/import
- **Goal:** Relocatable instance (paths relative to named anchors) + Export/Import bundling full config (+optionally staged mods).
- **What exists:** State serializes with mostly absolute paths; only the Steam prefix is normalized (`generalizeSteamPath`, `moddedapplication.cpp:913`). `exportConfiguration` writes *deployer + auto-tag metadata only* — not a relocatable instance.
- **Approach:** Named anchors (game dir, prefix, staging root); store deployer target/source + staging relative, resolved at load (extend `generalizeSteamPath` to all paths); `exportInstance`/`importInstance` bundling the whole per-app JSON (+ optional staging) via the installer's zip path; import rewrites anchors via a path-mapping dialog + validates.
- **Touch points:** `moddedapplication.cpp:913` + export/import; `applicationmanager.{h,cpp}`; new import dialog; installer zip.
- **Considerations:** Backward-compat reading old absolute paths; anchor fallback; staging bundle size; export the full state set (unlike current config-only).
- **Effort:** L.
- **Depends on:** builds on #13; CLI wrappers tie to #44.

### Issue #56 — Ship auto-tags for TW3/CP mod types
- **Goal:** Add `auto_tags` arrays to TW3 + CP2077 app configs so mods auto-classify (CET, RED4ext, redscript, ArchiveXL, TweakXL, REDmod, .archive, bundles, WitcherScripts, DLC) at install.
- **What exists:** **Pure data — no C++.** The engine is complete: `AutoTag`/`TagConditionNode` support `file_name`+`path`, regex, boolean `expression` (`autotag.h:23`, `tagconditionnode.h:18-34`); `AddAppDialog` parses `auto_tags` + offers "Import recommended" (`addappdialog.cpp:242-261,277,410`). Reference: `steam_app_configs/489830.json`. `292030.json`/`1091500.json` currently have zero `auto_tags`.
- **Approach:** Add `auto_tags` to `1091500.json` (path rules `bin/x64/plugins/cyber_engine_tweaks/*`→CET, `red4ext/*`→RED4ext, `r6/scripts/*`→redscript, `r6/tweaks/*`→TweakXL; file_name `.archive`→Archive, `.archive.xl`→ArchiveXL; `mods/*`/`info.json`→REDmod) and `292030.json` (`content/scripts/.*\.ws$`→WitcherScript, `content/.*\.bundle$`→Bundle, `input.xml`/`*.settings`→Menu/Config, `mod*`→Bundled, `dlc/`→DLC), mirroring `489830.json`'s schema.
- **Touch points:** `steam_app_configs/1091500.json`, `292030.json` (data only).
- **Effort:** S.
- **Depends on:** building block for #57.

### Issue #57 — Warn on missing CP2077/TW3 framework dependency
- **Goal:** At install/deploy, detect when an enabled mod needs a framework (ArchiveXL, TweakXL, redscript, CET, TW3 shared scripts) that no enabled mod provides; warn naming it.
- **What exists:** No framework-presence detection. `cyberpunk_setup` only checks the Proton prefix DLLs (`missingPrefixPrerequisites`), now wired read-only via `getCyberpunkSetupInfo`. The per-mod file scan exists (`AutoTag::readModFiles`, `autotag.h:157`); `checkModRules()` is the pre-deploy-warning precedent.
- **Approach:** Define per-game provides/needs signatures (needs-ArchiveXL = ships `*.archive.xl`; provides = `red4ext/plugins/ArchiveXL/`; similar for TweakXL/redscript/CET); reuse the file scan over enabled mods; in deploy (or a pre-deploy check mirroring `checkModRules`), warn non-blocking naming mod + framework via `sendGameToolResult`/log.
- **Touch points:** `cyberpunksetup.{h,cpp}` (extend) or new helper; `moddedapplication.cpp:43-100` + `getEnabledModPathsInLoadOrder` (`:2588`); `autotag.h:157`; `applicationmanager.cpp`.
- **Considerations:** Signature accuracy; non-blocking; only TW3/CP deployer types.
- **Effort:** M.
- **Depends on:** **#56 (shared path-classification signatures).**

### Issue #59 — Extract TW3 vanilla scripts for the 3-way merge base
- **Goal:** Supply the unpacked vanilla WitcherScript tree as the merge base so the wired `tw3_script_merge::mergeScripts` does a clean 3-way instead of conflict-heavy 2-way.
- **What exists:** `mergeScripts` already accepts an optional `vanilla_scripts_root` (`tw3scriptmerge.h:162-165`) but is called WITHOUT it (`moddedapplication.cpp:2611`); the result even says "no vanilla script base was supplied". No `content0`/bundle extraction anywhere.
- **Approach:** Add a persisted per-app/deployer "Vanilla scripts directory" setting; pass it as the 3rd arg to `mergeScripts`; a one-time "Extract vanilla scripts" helper (resolve game root from the `Tw3Deployer` target's parent; either invoke an external unpacker (wcc_lite/QuickBMS) via the `Tool`/Proton runner + cache, or let the user pick a pre-unpacked folder); keep the 2-way fallback + message when absent.
- **Touch points:** `moddedapplication.cpp:2604-2635` + settings (`:1807,2030`); `tw3scriptmerge.h:162`; UI setting; `Tool`/Proton runner.
- **Considerations:** Extraction tool availability/licensing — likely "point Limo at an unpacked tree" for v1 (matches the header's "caller must supply"); cache invalidation across game updates; merged folder still must be added as a high-priority mod.
- **Effort:** M.
- **Depends on:** complements the wired #48 merger; independent of #56/#57.

---

# Appendix A — Cross-cutting shared infrastructure

Seven building blocks several issues depend on. Build each once first to make per-issue work cheaper. **Suggested build order:** 1 & 4 (cheap prerequisites) → 2 & 3 (unblock #49/#50/#53/#54/#20) → 6 (unblocks #15/#27/#33/#35) → 7 (long pole; #49/#53/#54 depend on it).

**1. Version-comparison utility (S).** New `src/core/versionutils.{h,cpp}` with `isLess/isLessOrEqual/isNewerThan` (Qt-free `std::string`). Hoist `MainWindow::versionIsLessOrEqual` (`mainwindow.cpp:1451`, called `:1409`) into core; add explicit `isNewerThan` (the current predicate is really "not newer"). Do **not** conflate with `VersionChangelog::operator<` (compares by date). Consumed by #41, #47, #15.

**2. Worker "run task → report result / run command" pattern (S–M).** Generalize the already-present `ApplicationManager::sendGameToolResult(title,msg)` (`applicationmanager.h:550`) + `sendRunCommand(name,cmd)` (`:556`), handled by `onGameToolResult`→`QMessageBox` (`mainwindow.cpp:3493`) / `onRunGameCommand`→confirm-then-`runConcurrent` (`:3498`). Worker-slot shape to copy: `mergeTw3Scripts`/`getCyberpunkSetupInfo`/`deployRedMods` (`applicationmanager.cpp:802-853`) — validate ids → `handleExceptions(&ModdedApplication::m,…)` → emit → `completedOperations()`. M once a *structured* (non-string) result is needed (#50/#53) — new payload type (component 4) + a non-`QMessageBox` sink (component 3); rename away from "GameTool". Busy lifecycle is centralized (`onCompletedOperations`, `mainwindow.cpp:1893`); `runConcurrent` (`:1124`) is the command leg. Consumed by #49/#50/#53/#54/#59.

**3. Reusable bottom-panel / tab framework (M).** Pattern for a closeable bottom panel with sub-tabs, mirroring the log: `log_container`→`log_tab_widget`→`QPlainTextEdit`s (`mainwindow.ui:1184-1267`), toggled in `setupLog` (`mainwindow.cpp:1038-1100`). For model-backed panels reuse the conflicts-window construction (`mainwindow.cpp:527-540`). New top-level tabs follow the `app_tab_widget` + QSettings `current_tab` persistence (`mainwindow.cpp:1151`). Factor the duplicated log HTML/color helper if #20 touches it. Consumed by #20/#50/#11/#24.

**4. Cross-thread metatype convention (S/type).** Two-step: `Q_DECLARE_METATYPE(T)` at file scope (`mainwindow.cpp:50-74`) + `qRegisterMetaType<T>()` first in `setupConnections` (`:174-198`). New payload structs live in `src/core/` (like `DeployerInfo`/`ConflictInfo`/`ImportModInfo`). Add a "keep these two in sync" comment. Omitting registration makes queued signals silently drop. Consumed by #50/#53/#54/#15/#41.

**5. Settings/preferences expansion pattern (S/setting).** Add control to `settingsdialog.ui` → persist in `SettingsDialog::on_buttonBox_accepted` via `QSettings(QCoreApplication::applicationName()).setValue` → getter → pull in `MainWindow::onSettingsDialogComplete` (`mainwindow.cpp:2244-2257`) → default in `loadSettings` (`:1146`). Accept signal `settingsDialogAccepted` (wired `:696-700`). Use the `applicationName()` QSettings form (not the hard-coded `QSettings{"Limo"}` at `:2507`); follow `#ifdef LIMO_WITH_LOOT` guarding for build-optional settings. Consumed by #17/#47/#19 (last two also need a worker action → intersect component 2).

**6. Shared Nexus service layer (M).** Consolidate behind the static `nexus::Api` facade (`nexus/api.h`: `getNexusPage`/`getMod`/`getModFiles`/`getChangelogs`/`getTrackedMods`/`track`/`untrack`/`getDownloadUrl`/`validateKey`/URL helpers/`initModInfo`), run all on the worker via `handleExceptionsForFunction`. The work: stop UI from calling blocking `Api` directly (it does in `settingsdialog.cpp:324,366,401,422` + `changeapipwdialog.cpp:20,49,64,67`); route through `ApplicationManager` slots + one/two new payloads (+ metatypes). `Page` already crosses the thread. Consumed by #15/#27/#33/#35.

**7. Deploy snapshot / transaction primitive (L).** Refactor existing machinery into reusable pieces: the `.lmmfiles` manifest (`load/saveDeployedFiles`, `deployer.cpp:607,641`); the deploy transaction (`Deployer::deploy`, `:45-64`); and **the diff in `backupOrRestoreFiles` (`set_difference` of dest vs source) — split it out to get the dry-run primitive directly** ("files to add / to restore"). `BackupManager` already does recursive snapshot/restore with symlink preservation (the restore-points engine). `getExternallyModifiedFiles` (`deployer.cpp:830`) + `ExternalChangesInfo` are the integrity basis. The crux/L: extract a clean **non-mutating** diff (avoid the `rename`/`remove`/`saveDeployedFiles` writes). Consumed by #49/#53/#54.

---

# Appendix B — Packaging, CI & testing

Remaining "polish & packaging" — **not** a numbered issue. The Qt6 `build` job is green & gates merges; the `test` job (`continue-on-error: true`) is red & non-blocking. New features have **zero** tests; new menus are largely undocumented.

### 1. Get the test job green
The red is **not** the LOOT/OpenMW tests (excluded by `if(LIMO_WITH_LOOT)` with LOOT off) — it's the PR-199 tree refactor plus a logically broken test. (1) **Triage load-order failures** — audit `getTraversalItems()`/`swapChild()` expectations in `tests/test_deployer.cpp` (`swapChild` `:92-93,109`; "Mods are sorted" `:164-191`), `test_reversedeployer.cpp`, `test_moddedapplication.cpp`; **`test_moddedapplication.cpp` "Mods are uninstalled" (`:228-289`) is impossible** — `REQUIRE size()==1` (`:265`) then `==2` (`:277`) after one `uninstallMods({0,2})` with no intervening install — wrong, not stale. Fix expected trees or quarantine genuinely-mid-refactor cases with Catch2 `[!mayfail]`/`[!shouldfail]`. **(M)** (2) **Notes/pin round-trip** test (`setModNote`/`pin`/`unpin` survive reload; unpin clears; unknown-id no-op), mirroring "State is saved" (`test_moddedapplication.cpp:97`). **(S)** (3) **Rules + pruning-on-uninstall** test — `add/remove/setModRulesFor`/`checkModRules` + assert rules pruned on uninstall (currently a real bug — see #42). **(M)** (4) **Group name/notes/dissolve** — extend "Groups update loadorders" (`:143`): set name+notes, dissolve, assert vectors stay index-aligned + survive reload. **(S)** (5) **Merge/REDmod bridges** — test the free functions directly (`tw3_script_merge::mergeScripts`, `Tw3MergeUtil::mergeInputXml`, `cyberpunk_redmod::parseRedMod/layoutRedMods/redmodDeployCommand`): `LIMO_MERGE` idempotency, `.ws` conflict counts, REDmod discovery, **shell-escape regression guard for #34**; new fixtures under `tests/data/` + new `tests/test_tw3merge.cpp`/`test_cyberpunkredmod.cpp` in `tests/CMakeLists.txt`. **(L)** Then drop `continue-on-error: true` (`.github/workflows/ci.yml:70-73`) to make tests a gate.

### 2. Release packaging
**Exists:** Flatpak app-id + assets in `flatpak/` (desktop/metainfo/png), a parallel set in `install_files/`, and an `IS_FLATPAK` install branch (`CMakeLists.txt:419-442`). **Missing:** no flatpak-builder **manifest**, no AUR **PKGBUILD**, no **AppImage** recipe. (1) **Flatpak manifest** `flatpak/io.github.limo_app.limo.yml` (Qt6 runtime, cpr-from-source module like CI, Limo module with `-DIS_FLATPAK=ON -DLIMO_WITH_LOOT=OFF`); add a fork `<release>` to the metainfo (`:43-`). **(M)** (2) **AUR** `packaging/aur/PKGBUILD` + `.SRCINFO` (depends qt6-base/svg, jsoncpp, libarchive, pugixml, openssl, curl + cpr; `build()` mirrors README). **(M)** (3) **AppImage** `packaging/appimage/` (linuxdeploy + `-plugin-qt`, bundle Qt6 plugins + cpr `.so`). **(M)** (4) **Versioning** — three sources disagree & predate the fork (`APP_VERSION` `consts.h:3`, `install_files/changelogs.json`, metainfo `<releases>`); pick a fork scheme, add one synchronized release entry to all three, document "bump in lockstep". **(S)**

### 3. CI hardening
Single `build` job + non-blocking `test`; caches cpr but **no** apt/ccache cache, **no** lint, **no** matrix, **no** artifact. (1) **Matrix** — add a `LIMO_WITH_LOOT=ON`/`UNRAR=ON` axis (compiles the LOOT tests) ± clang. **(M)** (2) **clang-format gate** — add `.clang-format` (codebase is Allman, 2-space) + a `--dry-run --Werror` job, advisory then required. **(M)** (3) **Artifact upload** — `actions/upload-artifact@v4` for the binary, later AppImage/Flatpak. **(S)** (4) **Cache + dedup** — ccache/sccache + apt cache; factor the duplicated deps+cpr setup (`ci.yml:26-33,77-86`) into a composite action. **(M)**

### 4. Docs
`docs/` covers builds + the two games; `README.md` presents the fork but the new menus (commits `aaef842`, `c1ce289`) are undocumented and README still calls script-merge/REDmod "out of scope" (`:99,107-108`). (1) **README** — add "What this fork adds" (notes/pin, conflict dialog, mod rules, named groups, TW3/CP merge + Proton-setup); fix the stale "out of scope"/roadmap blocks (`:95-110`). **(S)** (2) **`docs/FEATURES.md`** — walk each new menu + the experimental caveats the methods already emit; cross-link from README + the game docs. **(M)** (3) **In-app changelog** — land the synchronized fork entry in `install_files/changelogs.json` so features are discoverable in-app. **(S)**

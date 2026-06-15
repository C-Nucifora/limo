# Big-feature implementation plan (Limo fork)

Subagent-ready plans for the **large / infrastructure** features that were deliberately *not* batched yet. Each entry is self-contained enough to hand to one subagent. For the smaller fork issues (#1–#59) see `PLAN.md`; for externally-sourced ideas see `docs/external-feature-ideas.md`.

## Status legend & current state
- **Done / merged to `dev`:** hermetic test suite, the wave-1/2 bug fixes, reflink backups (#130), alphabetical app list (#123), FOMOD link/image (#88/#89), autofill app id (#80), install-dialog filter (#146), resizable columns (#142), CP2077/TW3 auto-tags (#56, in PR).
- **In progress (agents/PRs, integrate first):** export mod list (#7), remember FOMOD choices (#135), GOG templates (#74), scriptable CLI (#44), overlay/ModFS deployer (#110), MO2/Vortex import (#45), third-party APIs (#60).
- **Planned below (NOT started):** #22 i18n, #99 MO2 Python plugins, #24 save manager, #114 OMM network repos, #33 in-app Nexus browser, #1/#2 Nexus Collections, #8 persistent download queue, the deploy-safety cluster (#49/#54/#53/#50), #46 hooks, #55 portable instances.

## Conventions every implementer MUST follow
- **Build:** `cmake -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=Release -DLIMO_WITH_LOOT=ON -DCMAKE_PREFIX_PATH=$PWD/../limo-deps/prefix && ninja -C build Limo` (libloot 0.29 + cpr vendored). Editor/LSP errors about `QDialog`/`cpr/cpr.h`/`std::format` are environment noise — trust `ninja`.
- **Tests:** `-DBUILD_TESTING=ON`, then `cd build && TMPDIR=/tmp ctest`. Suite is hermetic (copies fixtures to a temp dir); a run must leave `git status tests/data` clean. Keep it 60/60.
- **Prefer new files + minimal, append-only hooks** to shared files (`mainwindow.*`, `applicationmanager.*`, `moddedapplication.*`, `deployerfactory.*`) to avoid merge pain.
- **Architecture:** UI (`src/ui`) talks to `ApplicationManager` (Qt signal/slot, threaded via QtConcurrent) which owns `ModdedApplication` per app; deployers derive from `Deployer`; Nexus HTTP is in `src/core/nexus` using cpr + jsoncpp.
- Commit messages: no Claude/AI authorship trailer (user policy). Cite the fork issue + upstream issue.

---

## 1. #22 — Internationalization (Qt translations) — effort L, do LAST
**Why last:** touches nearly every UI file (`tr()` wrapping); will conflict with any other in-flight UI work. Schedule when UI churn is low.
**Goal:** translatable UI with at least one shipped non-English `.qm`.
**Steps:**
1. Wrap user-facing strings in `QObject::tr(...)` / `tr(...)` across `src/ui/**` (dialogs + mainwindow). Strings built via `std::format`/`+` must be converted to `tr("...%1...").arg(...)` so they're translatable and reorderable. Leave `Log::` and core (`src/core`) strings as-is (non-UI/diagnostic).
2. Add a `translations/` dir; in `CMakeLists.txt` use `qt_add_translations()` (Qt6) or `lupdate`/`lrelease` to generate `.ts` → `.qm`. Add a `limo_en.ts` + one real translation (e.g. `limo_de.ts`) seed.
3. In `main.cpp`, after `QApplication`, load a `QTranslator` for `QLocale::system()` (or a settings override) and `installTranslator`. Install the Qt base translator too.
4. Settings: add a language selector (System/English/…) persisted via QSettings; re-translate on change (retranslateUi) or prompt restart.
**Touch points:** all of `src/ui`, `main.cpp`, `CMakeLists.txt`, new `translations/`. **Risk:** churn; do in one focused pass. **Deliver incrementally:** the tr()-wrapping + build wiring + English .ts is the bulk; one sample translation proves the pipeline.

## 2. #99 — Run MO2 Python plugins — effort XL, research-spike first
**Goal:** load & run Mod Organizer 2 Python plugins inside Limo.
**Reality check:** MO2 plugins target MO2's C++/Python plugin API (PyQt + `mobase`), not Limo's. Full compatibility = reimplementing a large MO2 ABI. **Recommend a spike, not a full build.**
**Spike steps:**
1. Embed CPython (`pybind11` or raw CPython) behind a build flag `LIMO_WITH_PYTHON` (new `src/core/python/` dir, off by default). Add a tiny `mobase`-shaped shim exposing a *minimal* subset (IOrganizer: modList, pluginList, profile, paths).
2. Prototype loading one simple MO2 tool-plugin and invoking its `display()`.
3. Document exactly which `mobase` interfaces real plugins need (survey 5–10 popular ones) and the gap.
**Deliverable for now:** the spike + a written compatibility assessment in this repo (`docs/mo2-python-feasibility.md`). Do NOT promise full MO2 plugin compat without the spike's findings.
**Risk:** very high scope; isolate behind a flag so it never affects the default build.

## 3. #24 — Save-game manager tab — effort L
**Goal:** a "Saves" panel listing the active app/profile's save files with metadata + actions.
**Steps:**
1. New core: `src/core/savemanager.{h,cpp}` — given a saves directory (resolve from the Proton prefix / per-game known path; reuse #19 proton-prefix logic if present), enumerate save files, parse cheap metadata (timestamp, size; character/level/screenshot where the format is trivially parseable — keep parsing best-effort and game-agnostic first).
2. Persist per-profile a saves directory setting (new field in app config JSON; bump nothing else).
3. New UI: `src/ui/savemanagerwidget.{h,cpp,ui}` shown as a tab in mainwindow (append a tab + a small `ApplicationManager` passthrough to fetch saves). Actions: open-in-file-manager (QDesktopServices), delete (with confirm), copy/backup.
**Touch points:** new core + new widget; small `mainwindow`/`applicationmanager` hooks. **Risk:** save-format parsing is game-specific — start with timestamp/size/screenshot only.

## 4. #114 — OMM-compatible network mod repositories — effort L
**Goal:** connect to an Open-Mod-Manager network repository (domain + optional password), list mods with versions, download/update.
**Steps:**
1. Study the OMM repo XML format (https://github.com/iquercorb/OpenModMan/wiki/Create-and-manage-Network-Repository). New core `src/core/remote/ommrepository.{h,cpp}` using cpr to fetch the repo descriptor + parse (pugixml is already a dep) into the same provider-neutral structs introduced by #60 (`src/core/remote/remotesource.h`).
2. Implement: connect/auth, list packages + versions, resolve download URL, detect updates (version compare).
3. Persist configured repositories (URL + optional credential; store secrets like the Nexus key path, not plaintext).
4. UI: a "Repositories" section (new dialog or settings tab) to add/remove repos and a browse/list to install from. Reuse #33's browser UI if landed.
**Dependency:** ride on #60's `remote::` abstraction. **Risk:** auth/secret handling; `.omp` (=zip+1) extraction already works via libarchive.

## 5. #33 — In-app NexusMods browsing & search — effort L
**Goal:** search/browse the current game's Nexus mods and install directly.
**Steps:**
1. Extend `src/core/nexus/api.{h,cpp}`: add search (Nexus search endpoint), category/sort params, and result structs (reuse `nexus::Mod`). Respect the existing User-Agent/header helper.
2. New UI `src/ui/nexusbrowserdialog.{h,cpp,ui}`: search box + filters (sort by endorsements/downloads/recent, category), results list with lazy thumbnails (depends on #35 cache), a detail pane (description/images/files), and an Install action that routes through the existing download/import flow (`ImportModInfo`, `ApplicationManager` download path — the same one fixed in #70/#261).
3. Wire a toolbar/menu entry in mainwindow to open it for the current app's Nexus domain.
**Dependencies:** #35 thumbnails (optional), the download path. **Risk:** API rate limits; premium-only download links (handle the 403 like `getDownloadUrl`).

## 6. #1 / #2 — Nexus Collections import / export — effort L
**Goal:** install a Nexus Collection manifest and export the current setup as one.
**Steps (import #1):** parse `collection.json` (mod/file ids, versions, install order, rules); for each entry resolve the file via the Nexus API and route through the existing queued download+install; honor install-phase ordering and embedded rules (reuse the mod-rules system). **Export #2:** emit a `collection.json` from the current deployer load order + per-mod `remote_mod_id`/`remote_file_id`/phase.
**Touch points:** new `src/core/nexus/collection.{h,cpp}`; hooks in `moddedapplication`/`applicationmanager` for install-from-collection; a small UI action. **Depends on:** robust download queue (#8). **Risk:** large manifests; partial-failure handling (continue + report, don't abort).

## 7. #8 — Persistent download queue with progress & retry — effort L (foundational)
**Goal:** make the in-memory download queue persistent, with a Downloads panel (progress/status/retry/cancel) — also fixes the "can't cancel / lost on restart" complaints.
**Steps:** persist the queue to `_download/lmm_queue.json` on change/exit; resume incomplete downloads on startup; a Downloads widget (new `src/ui/downloadswidget.*`) bound to the queue with per-item progress/speed/cancel/retry. **Do this before #1/#2/#33** (they drive downloads through it). **Touch points:** the download/IPC path in `applicationmanager` + new widget. **Risk:** threading; partial-file resume.

## 8. Deploy-safety cluster — #49 dry-run, #54 restore points, #53 verify, #50 health-check — effort L (shared infra)
These share one core primitive: **diff the source-map (what should be deployed) against `.lmmfiles` (what is deployed)**.
1. Add `Deployer::computeDeploymentPlan()` returning {to-create, to-overwrite (with winning mod), to-remove, conflicts} without touching disk.
2. **#49 dry-run:** show the plan in a dialog before a real deploy.
3. **#54 restore points:** before each deploy, snapshot every deployer's load order + enabled/group state to a timestamped JSON; a UI list to roll back.
4. **#53 verify:** audit live target vs `.lmmfiles` (exists? still hardlinked/identical?); report drift.
5. **#50 health-check "Problems" panel:** aggregate orphaned files, broken links, unmet rules (reuse #42 rule check), conflict summary.
**Touch points:** `deployer.{h,cpp}` (plan/verify), `moddedapplication` (snapshots), new UI panels. **Sequence:** #49 first (anchors the diff), then #54/#53/#50 reuse it.

## 9. #46 — Pre/post-deploy hooks — effort M–L
**Goal:** user commands/scripts run automatically around deploy/undeploy. Store per-app hook commands (pre-deploy, post-deploy, pre/post-undeploy) in app config; run them via the existing `runCommand` path (which is now shell-escaped per #32/#34) around `deployMods`. UI: a hooks section in the app/settings dialog. **Risk:** command injection — reuse the existing `shellEscape` and never run unescaped user fields.

## 10. #55 — Portable / relocatable instances + export-import — effort L
**Goal:** store paths relative to a per-instance root where possible; "Export/Import instance" packaging app+deployers+profiles+tools+config (not the mod blobs by default). **Touch points:** path handling in `moddedapplication`/config, a packaging util, UI actions. **Risk:** absolute paths are pervasive — audit every stored path; provide a migration.

---

## Recommended global sequencing
1. **Integrate the in-flight agents** (#7, #135, #74, #44, #110, #45, #60) and stabilize.
2. **#8 download queue** (foundational) → unlocks **#1/#2 collections** and **#33 browser**.
3. **#33 + #35** (browser + thumbnails) and **#114** (reuse #60's `remote::` abstraction).
4. **Deploy-safety cluster (#49→#54→#53→#50)** as one project.
5. **#24 saves**, **#46 hooks**, **#55 portable** (independent, parallelizable).
6. **#22 i18n LAST** (string freeze), **#99 MO2-Python** only after a feasibility spike.

## How to dispatch (per feature)
Hand a subagent: this section + "build/test conventions" above + the explicit file scope. Require: build green, tests 60/60, new logic in new files, minimal append-only hooks, no AI-authorship trailer, commit on its own branch, report branch/SHAs + what's stubbed.

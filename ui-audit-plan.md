# Limo UI Audit Plan

**Date:** 2026-06-15  
**Scope:** `src/ui/` — all `.cpp`, `.h`, and `.ui` files  
**Approach:** Static code and markup analysis; no build or test execution.

---

## 1. Executive Summary

The UI is structurally functional and covers a wide feature surface (mod management, deployers, profiles, tags, backups, NexusMods integration, FOMOD installer). The core architecture — `MainWindow` ↔ `ApplicationManager` over Qt signals/slots across a worker thread — is sound and thread-safe by design.

However, the codebase has grown around a single massive god-object (`mainwindow.cpp`, 3 538 lines; header 1 606 lines) that owns all dialogs, all models, all context menus, all toolbar actions, all business-state flags, and all signal wiring. This creates compounding problems across every dimension of the audit:

**Top themes:**

1. **God-object mainwindow.** More than 1 300 lines of private member declarations and 120+ slot/signal declarations. Every new feature adds another member variable, another dialog pointer, another pair of `enable…()` calls across multiple enable-guard methods.
2. **No empty-state guidance.** First-run and empty-app UX is rudimentary: the AddAppDialog is auto-opened but there is no onboarding copy, no placeholder text in the four main tabs, and no help surface (relates to issues #92, #25).
3. **Settings stored in two places.** `SettingsDialog` reads/writes `QSettings` directly, and `MainWindow::loadSettings()` also reads the same keys. The mapping between the two is implicit and duplicated, making it easy for them to drift.
4. **Hardcoded colors** immune to system themes. The `colors` namespace hardcodes Tableau palette hex values applied as foreground brushes; these are neither palette-relative nor switchable (relates to issue #17).
5. **Tri-state checkboxes as filters** without clear affordance. The three-state `Qt::CheckState` pattern for filters (unchecked = off, partially-checked = include, checked = exclude) is non-standard and undocumented in the UI.
6. **No keyboard navigation completeness.** `ModListView::moveCursor` returns `currentIndex()` unconditionally, disabling all keyboard arrow navigation in both list views.

---

## 2. Usability / UX Findings

### UX-01: Keyboard navigation disabled in both list views
**Problem:** `ModListView::moveCursor` (`src/ui/modlistview.cpp:223`) returns `currentIndex()` for every `CursorAction`, making arrow-key navigation a no-op. `DeployerListView` inherits this behaviour.  
**Where:** `src/ui/modlistview.cpp:223`  
**Suggested fix:** Remove the override entirely, or delegate to `QTableView::moveCursor` for row-change actions and only intercept column-change actions to lock columns.  
**Effort:** S

### UX-02: Empty-state UX — no onboarding copy or placeholder content
**Problem:** When no applications exist, `initUiWithoutApps(false)` disables most widgets and `onGetApplicationNames` auto-opens `AddAppDialog`. There is no explanatory text, no screenshot, no tooltip explaining what a "staging directory" is, and no progressive disclosure of deployer/profile/backup concepts. Relates to issues #92 (onboarding) and #25 (empty-state guidance).  
**Where:** `src/ui/mainwindow.cpp:1254`, `src/ui/mainwindow.ui` (all four tabs)  
**Suggested fix:** Add a centred placeholder widget per tab (QStackedWidget pattern) that is shown when the list is empty. On the App tab, show a short "Get started" text with a "New Application" button.  
**Effort:** M

### UX-03: Tri-state filter checkboxes are not self-explanatory
**Problem:** The active-mod, group, and update filter checkboxes use three states with non-obvious semantics: unchecked = off, partially-checked = show only matching, checked = hide matching. This is inverted from most conventions and is not labelled anywhere.  
**Where:** `src/ui/mainwindow.cpp:3023–3060`, filter widget in `.ui` (filter area)  
**Suggested fix:** Add a `?` button or tooltip next to the filter section explaining the three-state convention. Alternatively replace with two explicit radio buttons ("Show only", "Hide") per filter category.  
**Effort:** S

### UX-04: "Check for Mod Updates" button hidden unless Nexus API key is valid
**Problem:** `ui->check_mod_updates_button` is hidden entirely (`mainwindow.cpp:1106`) when no valid API key is configured. Users who have not set up Nexus integration will not know this feature exists. Relates to issue #33 (in-app browser / NexusMods integration discoverability).  
**Where:** `src/ui/mainwindow.cpp:1106`  
**Suggested fix:** Show the button but disable it, with a tooltip: "Requires a NexusMods API key (Settings → NexusMods)." This preserves discoverability.  
**Effort:** S

### UX-05: Log window is invisible at startup and hard to discover
**Problem:** The log is toggled via a small flat "Log" button embedded in the status bar (`setupLog`, `mainwindow.cpp:985`). There is no keyboard shortcut, no tray notification, and no way to know errors occurred without the `show_log_on_error_` setting being enabled. Relates to issue #20 (log viewer).  
**Where:** `src/ui/mainwindow.cpp:985`  
**Suggested fix:** Add a `Ctrl+L` shortcut for the log panel. Display an unread-error indicator (count badge or coloured icon) on the log button when errors have arrived while the panel is hidden.  
**Effort:** S–M

### UX-06: Confirmation dialogs reuse a single `message_box_` instance with manual checkbox manipulation
**Problem:** `MainWindow` owns one `std::unique_ptr<QMessageBox> message_box_`. Before each use it manually sets `checkBox()->setHidden(true/false)`, `setText`, `setCheckState`, etc. (`mainwindow.cpp:2313–2590` and elsewhere). This is error-prone: one call omitting `setHidden` inherits the previous state.  
**Where:** `src/ui/mainwindow.cpp` — every `onRemove*` handler  
**Suggested fix:** Extract a helper `bool askConfirm(const QString& text, const QString& checkboxText, bool* suppress)` that constructs a local `QMessageBox` per call, avoiding shared mutable state.  
**Effort:** S

### UX-07: "Don't ask again" confirmation state is per-category but not persisted on the fly
**Problem:** The `ask_remove_mod_`, `ask_remove_profile_`, etc. flags are only written to `QSettings` inside `closeEvent`. If the app crashes after a user ticks "Don't ask again", the preference is lost.  
**Where:** `src/ui/mainwindow.cpp:107–125` (closeEvent), `mainwindow.cpp:2729`  
**Suggested fix:** Write the flag to `QSettings` immediately when it changes, not only in `closeEvent`.  
**Effort:** S

### UX-08: No progress feedback for long-running operations beyond indeterminate bar
**Problem:** During deployment, an indeterminate `QProgressBar` is shown until `updateProgress` receives the first percentage. There is no status message identifying which deployer or file is being processed. The estimated time remaining (`updateProgress`, `mainwindow.cpp:2883`) is calculated but only shown in the progress bar format string, not in the status bar.  
**Where:** `src/ui/mainwindow.cpp:2883`  
**Suggested fix:** Show the current operation name (e.g., "Deploying 'Data Files' (2/4)") in the status bar. The ETA string could also appear there.  
**Effort:** S

### UX-09: Tab titles are bare single words; no mod count or status indicator
**Problem:** The four tabs are labelled "App", "Mods", "Deployers", "Backups" with no live counts or status. In MO2 and Vortex, the mod tab shows total/active counts in the header.  
**Where:** `src/ui/mainwindow.ui` (tab widget)  
**Suggested fix:** Append `(N)` counts to "Mods (N)" and "Deployers (N)" tab titles. Update them in `updateModList` and `updateDeployerList`.  
**Effort:** S

### UX-10: Context menu actions sorted alphabetically, not by frequency or logical grouping
**Problem:** Both `mod_list_menu_` and `deployer_list_menu_` sort actions with `sort_actions` lambda alphabetically (`mainwindow.cpp:510–541`). This means destructive actions ("Remove Mods") and navigation actions ("Browse Mod Files") appear interleaved with editing actions.  
**Where:** `src/ui/mainwindow.cpp:508`  
**Suggested fix:** Group actions with separators: primary actions, then navigation, then destructive (with separator before). Remove alphabetical sort or keep it only within groups.  
**Effort:** S

### UX-11: FOMOD dialog "Next" / "Finish" button ambiguity
**Problem:** The FOMOD installer's next/finish button (`fomoddialog.cpp:236`) changes text dynamically. The "Back" button is hidden initially. There is no step counter ("Step 2 of 4") shown to the user, making it impossible to gauge progress through a multi-step installation.  
**Where:** `src/ui/fomoddialog.cpp:235`  
**Suggested fix:** Add a `QLabel` showing "Step N of M" updated in `updateInstallStep`. Show `back_button_` always (disabled on step 1) rather than hiding it.  
**Effort:** S

### UX-12: Mod list uses delete-button column (col 0) as primary remove action, not Delete key
**Problem:** Removal in `ModListView::mouseReleaseEvent` fires on col 0 click (`modlistview.cpp:92`). The `Delete` key is not wired. Multi-select removal is only available via context menu ("Remove Mods"). There is no Ctrl+Z undo.  
**Where:** `src/ui/modlistview.cpp:92`  
**Suggested fix:** Add a `QShortcut(QKeySequence::Delete, this)` in the mod list that triggers the same removal path. (Undo is a larger architectural concern.)  
**Effort:** S

### UX-13: Window title inconsistency — `.ui` says "Linux Mod Manager", code says "Limo"
**Problem:** `mainwindow.ui:17` sets `windowTitle` to "Linux Mod Manager" but `MainWindow::MainWindow` immediately overwrites it to "Limo" (`mainwindow.cpp:91`) and `onGetDeployerInfo` sets it to "AppName - Limo". The `.ui` value is misleading to anyone reading the file.  
**Where:** `src/ui/mainwindow.ui:17`, `src/ui/mainwindow.cpp:91`  
**Suggested fix:** Update `mainwindow.ui` to match "Limo" so the `.ui` file is the single source of truth.  
**Effort:** S (trivial)

---

## 3. Visual / Layout Findings

### VL-01: Hardcoded Tableau palette colours ignore system themes
**Problem:** `colors.h` defines six hex constants (`BLUE 0x1f77b4`, `RED 0xd62728`, etc.) applied as `QBrush` foregrounds in `ModListModel::data` (green for updates), `FomodDialog` (red for unavailable, orange for required), and the log pane. These colours are invisible or confusing on some dark themes and are entirely disconnected from `QPalette`. Relates to issue #17.  
**Where:** `src/ui/colors.h`, `src/ui/modlistmodel.cpp:103`, `src/ui/fomoddialog.cpp:136`  
**Suggested fix:** Derive semantic colours from `QPalette::Highlight`, `QPalette::Link`, etc. For log severity, use palette-relative adjustments. For FOMOD option types, use `QPalette::PlaceholderText` or similar instead of fixed red.  
**Effort:** M

### VL-02: Progress bar container uses a magic 375px left spacer
**Problem:** `setupProgressBar` (`mainwindow.cpp:2206`) calls `layout->insertSpacing(0, 375)` with a hardcoded pixel value. On wide screens the bar sits off-centre; on narrow screens it overflows.  
**Where:** `src/ui/mainwindow.cpp:2206`  
**Suggested fix:** Replace the fixed spacer with a centred layout using `Qt::AlignCenter` or a `QSizePolicy::Expanding` spacer on both sides.  
**Effort:** S

### VL-03: `conflicts_window_` is a raw top-level QWidget with hardcoded 1200×600 size
**Problem:** `setupLists` creates `conflicts_window_ = new QWidget()` and calls `conflicts_window_->resize(1200, 600)` (`mainwindow.cpp:498`). This window is not a QDialog, has no close button title bar integration, and sets a fixed size that may exceed small displays.  
**Where:** `src/ui/mainwindow.cpp:486–498`  
**Suggested fix:** Promote to `QDialog` with `setSizeGripEnabled(true)` and let it be a child of `MainWindow`. Remove the hardcoded resize.  
**Effort:** S

### VL-04: `SettingsDialog` fixed at 500×350 in `.ui`, clips LOOT URL list
**Problem:** `settingsdialog.ui` sets geometry to 500×350. The LOOT masterlist URL scroll area inside contains 11 game entries which overflow significantly. The inner scroll area content height is set to 628px (`settingsdialog.ui:76`), making scrolling mandatory within a small fixed dialog.  
**Where:** `src/ui/settingsdialog.ui:4–12`  
**Suggested fix:** Remove fixed geometry from `SettingsDialog` and set `sizeHint` via `QTabWidget` minimum size instead, or allow the dialog to resize freely.  
**Effort:** S

### VL-05: Tab widget custom stylesheet computed in code, not in a style file
**Problem:** `setTabWidgetStyleSheet` (`mainwindow.cpp:1121`) computes a border colour by interpolating window/text palette colours at a 75% factor and sets it as a string stylesheet. This logic will not update if the user switches system themes while the app is running.  
**Where:** `src/ui/mainwindow.cpp:1121`  
**Suggested fix:** Connect to `QApplication::paletteChanged` and call `setTabWidgetStyleSheet` again, or use a stylesheet that references `palette()` tokens directly (e.g., `QTabWidget::pane { border-top: 1px solid palette(mid); }`).  
**Effort:** S

### VL-06: `info_deployer_list` (App tab deployer table) has no minimum column width on "Target" column
**Problem:** The "Target" column in `info_deployer_list` (`mainwindow.cpp:1860`) calls `resizeColumnToContents`, which can produce columns several hundred pixels wide for deep filesystem paths, making the table hard to read without scrolling. There is no ellipsis or truncation on long paths.  
**Where:** `src/ui/mainwindow.cpp:1860`  
**Suggested fix:** Cap the "Target" column at a reasonable maximum (e.g., 300px) and use a delegate that draws the path with leading ellipsis.  
**Effort:** S

### VL-07: Row-count label embeds string formatting in the model
**Problem:** `ModListProxyModel::updateRowCountLabel` (`modlistproxymodel.cpp:155`) calls `row_count_label_->setText("Mods displayed: " + ...)`. This wires a UI string into the proxy model and makes translation harder. The same pattern exists in `DeployerListProxyModel`.  
**Where:** `src/ui/modlistproxymodel.cpp:155`, `src/ui/deployerlistproxymodel.cpp`  
**Suggested fix:** Emit a `rowCountChanged(int)` signal from the proxy and let the view/window set the label text.  
**Effort:** S

---

## 4. Code Structure / Refactoring

### CS-01: MainWindow is a god object (3 538 cpp + 1 606 h lines)
**Problem:** `MainWindow` declares:
- 20+ `std::unique_ptr<QDialog>` members (all reusable dialogs owned here)
- 9 `bool ask_remove_*` flags
- 7 model/proxy/delegate pointers
- 6 `QAction*` members for app/deployer/profile management
- 4 context menu pointers
- ~120 public and private slots/signals
- `setupConnections()` alone is ~250 lines (`mainwindow.cpp:166–413`)
- Business logic interspersed with UI wiring (version comparison at `mainwindow.cpp:1337`, settings migration at `mainwindow.cpp:1291`, root-level condition loading at `mainwindow.cpp:1356`, process execution at `mainwindow.cpp:993`)

All four tabs (App/Mods/Deployers/Backups) and all feature subsystems are handled in one class.  
**Where:** `src/ui/mainwindow.cpp`, `src/ui/mainwindow.h`  
**Suggested fix (staged):**
1. Extract `AppTabController`, `ModsTabController`, `DeployerTabController`, `BackupTabController` — each owning the dialogs, models, and slots for its tab.
2. Move `loadSettings`/`closeEvent` settings I/O into a `UiSettings` helper.
3. Move `runCommand`/`runConcurrent` into a `CommandRunner` utility.
4. Move `versionIsLessOrEqual` and `updateOutdatedSettings` to a `MigrationHelper`.  
**Effort:** L

### CS-02: `SettingsDialog` reads and writes `QSettings` directly, duplicated by `MainWindow::loadSettings`
**Problem:** The same keys (e.g., `ask_remove_mod`, `deploy_for_all`, `log_level`, all LOOT URLs) are read in both `MainWindow::loadSettings` (`mainwindow.cpp:1036`) and `SettingsDialog::init` (`settingsdialog.cpp:36`). They are written in `SettingsDialog::on_buttonBox_accepted` and then re-read from `SettingsDialog`'s getter methods in `MainWindow::onSettingsDialogComplete`. This triple indirection creates two opportunities for drift.  
**Where:** `src/ui/mainwindow.cpp:1036`, `src/ui/settingsdialog.cpp:34–108`  
**Suggested fix:** Create a `AppSettings` value type holding all settings fields. `SettingsDialog` operates on a copy of this struct and emits it on accept. `MainWindow` applies it in one place.  
**Effort:** M

### CS-03: `ApplicationManager` header is 1 050 lines with all slot signatures in the header
**Problem:** `applicationmanager.h` contains all slot implementations' full parameter lists, and the `handleExceptions` template is defined in the header. This produces very long compile times and a header that effectively duplicates the signal contract of `MainWindow`.  
**Where:** `src/ui/applicationmanager.h`  
**Suggested fix:** Move the `handleExceptions` template to a `applicationmanager_detail.h` included only by the `.cpp`. Consider splitting large slot groups (backup, nexus, deploy) into pimpl-style helpers.  
**Effort:** M

### CS-04: `setupConnections()` is a 250-line monolithic clang-format-off block
**Problem:** The `// clang-format off` / `// clang-format on` pair at `mainwindow.cpp:165` wraps all 60+ `connect()` calls. The pragma exists because the formatter produces very long lines for these calls. The real problem is the number of connections.  
**Where:** `src/ui/mainwindow.cpp:165`  
**Suggested fix:** Split into per-subsystem `setupXxxConnections()` helpers called from `setupConnections`. The clang-format exemption can be dropped once individual connection blocks are shorter.  
**Effort:** S

### CS-05: `enableModifyApps`, `enableModifyDeployers`, `enableModifyBackups`, `enableModifyProfiles` — quadruplicated enable-guard pattern
**Problem:** `setBusyStatus` calls all four `enableModify*` methods which each iterate their own QAction lists and set individual widget states (`mainwindow.cpp:1149–1203`). Adding a new UI element requires updating up to four methods. Any mismatch leads to an element that stays enabled when it should not.  
**Where:** `src/ui/mainwindow.cpp:1149`  
**Suggested fix:** Collect all "disable-when-busy" widgets and actions into a `QList<QWidget*> busy_disabled_widgets_` and `QList<QAction*> busy_disabled_actions_` maintained at setup time. `setBusyStatus` then iterates those lists.  
**Effort:** M

### CS-06: Dual proxy-model pattern has asymmetric APIs
**Problem:** `ModListProxyModel` and `DeployerListProxyModel` implement nearly identical tag-filter and row-count-label APIs, but with slightly different method names and parameter orderings. `ModListProxyModel::addFilter(FilterMode, bool invalidate)` takes two params; `DeployerListProxyModel::addFilter` may differ. Both embed a `QLabel*` in the constructor, a coupling that makes them untestable without a QApplication.  
**Where:** `src/ui/modlistproxymodel.h`, `src/ui/deployerlistproxymodel.h`  
**Suggested fix:** Extract a `BaseListProxyModel` (or a `TagFilterMixin`) with the shared filter logic. Remove the `QLabel*` dependency and use a `rowCountChanged(int)` signal instead.  
**Effort:** M

### CS-07: `onGetAppInfo` is 120+ lines of direct widget manipulation
**Problem:** `onGetAppInfo` (`mainwindow.cpp:1783`) rebuilds the entire App tab by directly inserting `QTableWidgetItem` and `QPushButton`/`TablePushButton` widget cells, recreates tag checkboxes, re-populates deployer paths, and reinitializes root-level conditions — all in one slot. This makes the function hard to test and changes difficult to isolate.  
**Where:** `src/ui/mainwindow.cpp:1783`  
**Suggested fix:** Extract `refreshDeployerTable(AppInfo)`, `refreshToolTable(AppInfo)`, and `refreshTagCheckboxes(AppInfo)` helpers. The `info_deployer_list` and `info_tool_list` could be replaced with proper model-based `QTableView`s (currently they are `QTableWidget`s built manually).  
**Effort:** M

### CS-08: `AddModDialog` owns `FomodDialog` — dialog chain is tight-coupled
**Problem:** `AddModDialog` creates a `std::unique_ptr<FomodDialog>` (`addmoddialog.cpp:31`) and shows it after parsing the FOMOD signature. If FOMOD installation is completed, the signal chain is `FomodDialog::addModAccepted` → `AddModDialog::onFomodDialogComplete` → `AddModDialog::addModAccepted` → `MainWindow::onAddModDialogAccept`. This three-hop forwarding means the FOMOD dialog cannot be opened independently or reused.  
**Where:** `src/ui/addmoddialog.cpp:31`  
**Suggested fix:** Have `MainWindow` (or a future mod installation controller) coordinate the two dialogs directly. `AddModDialog` emits a "needs FOMOD" signal, `MainWindow` opens `FomodDialog`, and `MainWindow` collects the result.  
**Effort:** M

### CS-09: Business logic in the UI layer — version comparison, settings migration, root-level detection
**Problem:** `versionIsLessOrEqual` (`mainwindow.cpp:1337`) implements a version string parser. `updateOutdatedSettings` (`mainwindow.cpp:1291`) performs data migration using hardcoded URLs. `initRootLevelConditions` (`mainwindow.cpp:1356`) reads and parses JSON from disk. None of these belong in a view class.  
**Where:** `src/ui/mainwindow.cpp:1291, 1337, 1356`  
**Suggested fix:** Move version comparison to a utility in `src/core`. Move migration to an `AppMigration` class in the core or a separate migration module. Move root-level condition loading to `ApplicationManager` or its own loader.  
**Effort:** M

### CS-10: `TableCellDelegate` is tightly coupled to `ModListView`
**Problem:** `TableCellDelegate` casts its parent directly to `ModListView*` in its constructor (`tablecelldelegate.cpp:9`) and calls `parent_view_->getHoverRow()`, `isInDragDrop()`, `mouseInUpperHalfOfRow()` etc. in `paint`. This makes the delegate impossible to use with any other view.  
**Where:** `src/ui/tablecelldelegate.cpp:9`  
**Suggested fix:** Define an interface (abstract base or the needed methods as parameters) so the delegate can be tested and reused independently.  
**Effort:** S–M

### CS-11: `on_actionbrowse_mod_files_triggered` detects current tab by tab text string comparison
**Problem:** At `mainwindow.cpp:2644`, the slot checks `cur_tab == "Mods"` and `cur_tab == "Deployers"` by string comparison. If tab titles are ever changed (e.g., localised or to include counts per UX-09), this breaks silently.  
**Where:** `src/ui/mainwindow.cpp:2644`  
**Suggested fix:** Compare by tab index against the existing `mods_tab_idx` / `deployer_tab_idx` constants.  
**Effort:** S (trivial)

### CS-12: Settings keys are raw string literals spread across multiple files
**Problem:** The same setting keys (`"ask_remove_mod"`, `"deploy_for_all"`, `"fo3_url"`, etc.) are spelled as raw `const char*` literals in `mainwindow.cpp`, `settingsdialog.cpp`, and `settingsdialog.h`. A typo causes a silent default-value fallback.  
**Where:** `src/ui/mainwindow.cpp:1036`, `src/ui/settingsdialog.cpp:36`  
**Suggested fix:** Define all keys as `constexpr` string constants in a single `settings_keys.h` header and use those constants everywhere.  
**Effort:** S

---

## 5. Quick Wins vs. Larger Refactors

### Quick Wins (S effort, low risk, high visibility)

- **UX-01** — Restore keyboard navigation: remove `moveCursor` override.
- **UX-03** — Document tri-state filter semantics with tooltip or label.
- **UX-04** — Show "Check for Updates" button as disabled (not hidden) when no API key.
- **UX-05** — Add `Ctrl+L` shortcut for log panel; add error-count badge on log button.
- **UX-07** — Persist "Don't ask again" flag to QSettings immediately, not only on close.
- **UX-08** — Add operation name to status bar during deployment progress.
- **UX-09** — Add live mod/deployer counts to tab labels.
- **UX-10** — Group context menu actions with separators; remove alphabetical sort.
- **UX-11** — Show step counter in FOMOD dialog; always show Back button (disabled on step 1).
- **UX-12** — Wire `Delete` key to mod removal in `ModListView`.
- **UX-13** — Fix window title in `.ui` from "Linux Mod Manager" to "Limo".
- **VL-02** — Replace hardcoded 375px progress bar spacer with proper expanding spacers.
- **VL-03** — Promote `conflicts_window_` to `QDialog`; remove hardcoded `resize(1200, 600)`.
- **VL-04** — Remove fixed 500×350 geometry from `SettingsDialog`.
- **VL-05** — Connect `paletteChanged` to re-apply tab widget stylesheet.
- **VL-06** — Cap "Target" column width and add ellipsis delegate.
- **VL-07** — Emit `rowCountChanged(int)` from proxy models instead of calling `setText` directly.
- **CS-04** — Split `setupConnections` into per-subsystem helpers.
- **CS-11** — Replace string-based tab detection with tab-index constants.
- **CS-12** — Extract settings keys to `constexpr` constants in a shared header.
- **UX-06** — Extract `askConfirm()` helper to replace the reused `message_box_` pattern.

### Larger Refactors (M–L effort, higher risk, foundational)

- **CS-01** — Split `MainWindow` into per-tab controllers (`AppTabController`, `ModsTabController`, `DeployerTabController`, `BackupTabController`). This is the highest-leverage refactoring but touches every feature.
- **CS-02** — Introduce `AppSettings` value type; remove duplicated QSettings reads between `MainWindow::loadSettings` and `SettingsDialog::init`.
- **CS-05** — Consolidate enable-guard logic into a single disability list maintained at setup time.
- **CS-06** — Extract `BaseListProxyModel`; remove `QLabel*` dependency from proxy constructors.
- **CS-07** — Break up `onGetAppInfo` into targeted refresh helpers; replace `QTableWidget` with model-based `QTableView` for deployers and tools.
- **CS-08** — Remove `FomodDialog` ownership from `AddModDialog`; coordinate from a higher-level controller.
- **CS-09** — Move `versionIsLessOrEqual`, `updateOutdatedSettings`, and root-level condition loading out of `MainWindow` into core or dedicated helpers.
- **VL-01** — Full theme-awareness overhaul: replace hardcoded `colors::*` with palette-relative colours throughout models, delegates, and dialogs.
- **UX-02** — Implement empty-state placeholder widgets and first-run onboarding.
- **CS-10** — Decouple `TableCellDelegate` from `ModListView` via an interface/parameters.

---

## 6. Suggested Sequencing

### Phase 1: Low-risk, high-visibility polish (1–2 sprints)

Focus on the quick wins that are self-contained and immediately user-facing. Doing these first demonstrates progress and improves day-to-day usability without touching architectural concerns.

1. **UX-01** (keyboard navigation) — one-line fix with high impact.
2. **UX-13, CS-11, CS-12** — trivial correctness and naming fixes.
3. **UX-06 / UX-07** (confirmation dialogs) — extract `askConfirm`, write setting immediately.
4. **UX-03, UX-04, UX-05** (filter labels, hidden button, log shortcut).
5. **UX-08, UX-09, UX-10, UX-11, UX-12** (status bar, tab counts, context menus, FOMOD UX, Delete key).
6. **VL-02, VL-03, VL-04, VL-05, VL-06, VL-07** (layout and visual fixes).

### Phase 2: Settings and proxy model cleanup (1 sprint)

These reduce duplication and lay groundwork for testability without requiring large architectural changes.

7. **CS-02** (AppSettings value type).
8. **CS-04** (split `setupConnections`).
9. **CS-06** (BaseListProxyModel, remove QLabel coupling).
10. **CS-07** (break up `onGetAppInfo`).
11. **CS-10** (decouple TableCellDelegate).

### Phase 3: Theme support and empty-state UX (1 sprint)

12. **VL-01** (palette-relative colours — do after Phase 1 so colour usage is consistent).
13. **UX-02** (empty-state / onboarding widgets).

### Phase 4: Architectural decomposition (ongoing, 2–4 sprints)

Do this incrementally by extracting one controller at a time while keeping all tests green.

14. **CS-09** (move business logic out of MainWindow).
15. **CS-08** (decouple FomodDialog from AddModDialog).
16. **CS-05** (consolidate enable-guard logic).
17. **CS-01** (tab controller extraction — largest change, do last).

---

*This plan is grounded in static analysis of the source files at the commit checked out in this worktree. Line numbers reference the code as-read; they may shift as other changes land.*

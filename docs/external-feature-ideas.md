# External Feature Ideas for Limo

Curated from the issue trackers of Vortex (`Nexus-Mods/Vortex`), NexusMods.App
(`Nexus-Mods/NexusMods.App`), and MO2 (`ModOrganizer2/modorganizer`). Triaged
against the ~138 existing Limo fork issues and the upstream triage in
`upstream-issues.md`.

---

## Group 1 — Mod Discovery & Download

### 1. Download speed limiter
**Description:** Add a configurable cap on download bandwidth in Settings (slider
or numeric input). When Limo is fetching large archives from Nexus it can
saturate a connection and interfere with other traffic, which is especially
noticeable on Linux desktop environments where no system-level cap is easy to
set per-application.  
**Linux relevance:** Desktop responsiveness and coexistence with game streaming /
updates matters more on Linux where no OS-level download scheduler exists for
user apps.  
**Effort:** S  
**Source:** Nexus-Mods/NexusMods.App#3674

---

### 2. Persist download history across sessions
**Description:** Completed downloads currently vanish from the queue when the
app is restarted. Persist the completed download list (archive name, mod, date)
so users can reference what they installed in a previous session and avoid
re-downloading already-cached archives.  
**Linux relevance:** Session interruptions are common under Wayland compositors
and rolling-release distros; not losing context on restart is a quality-of-life
baseline.  
**Effort:** S–M  
**Source:** Nexus-Mods/NexusMods.App#3925  
**Note:** Limo already has a download queue (issue #8); this is about *history
persistence*, not the queue itself — not a duplicate.

---

### 3. Clear/hide completed downloads from the queue panel
**Description:** Once downloads are finished the queue panel fills with stale
entries. Add a "Clear completed" button (and/or auto-hide toggle) so the view
stays focused on active or failed transfers.  
**Linux relevance:** No OS download manager integration on Linux means Limo's
own panel is the only place to track downloads.  
**Effort:** S  
**Source:** Nexus-Mods/NexusMods.App#3924

---

## Group 2 — Mod List UX

### 4. Resizable mod-list columns
**Description:** Allow users to drag column dividers to resize individual
columns in the mod list (name, version, size, install date, etc.). Fixed-width
columns waste horizontal space and truncate long mod names.  
**Linux relevance:** Qt `QTreeView`/`QHeaderView` makes this trivial to expose;
many Linux users run non-maximised windows on tiling WMs where column width
matters.  
**Effort:** S  
**Source:** Nexus-Mods/Vortex#23247

---

### 5. Highlight conflicting mods with colour coding in the mod list
**Description:** When a mod is selected/highlighted, tint the rows of mods it
wins conflicts against (green) and loses conflicts to (red), giving instant
visual feedback without opening the per-file conflict detail dialog.  
**Linux relevance:** Same rationale as on any platform; especially useful for
power users running Limo in a terminal+GUI setup where switching dialogs is
costly.  
**Effort:** M  
**Source:** Nexus-Mods/Vortex#16178  
**Note:** Limo has a per-mod conflict detail view (#4) but no in-list colour
highlight — distinct feature.

---

### 6. Permanently ignore updates for specific mods
**Description:** A right-click "Ignore updates" toggle that suppresses the
update badge for a chosen mod persistently across restarts. Users who pin a
known-good version should not see perpetual update noise. Complement with a
filterable "ignored updates" list so they can be un-ignored later.  
**Linux relevance:** Same need as on other platforms; Limo already has version
pin (#9) to suppress notifications, but has no per-mod *update ignore* that
survives restarts — distinct feature.  
**Effort:** S  
**Source:** Nexus-Mods/Vortex#23219, Nexus-Mods/NexusMods.App#4032

---

### 7. Prune duplicate / outdated archive versions in one action
**Description:** A "Remove old versions" bulk action that, for every mod where
multiple downloaded archives exist, deletes all but the most-recently-installed
version. Reduces disk use without requiring manual hunt-and-delete.  
**Linux relevance:** Staging directories on Linux are often on limited-size
partitions; automated cleanup is a quality-of-life gain.  
**Effort:** S–M  
**Source:** Nexus-Mods/Vortex#19003

---

## Group 3 — File & Archive Operations

### 8. Search / filter inside the file-selection dialog (advanced install)
**Description:** When installing a large archive and choosing which files/folders
to include, add a search box that filters the tree in real time. Some mod
bundles contain dozens of optional files and the current list requires manual
scrolling.  
**Linux relevance:** No unique Linux angle, but Limo already has file-selection
on install (#75); adding search is a low-effort improvement to an existing
dialog.  
**Effort:** S  
**Source:** Nexus-Mods/NexusMods.App#3815

---

### 9. Export original (pre-FOMOD) mod archive from the staging area
**Description:** Add a right-click "Export archive" action that packages the
full original content of a staged mod (all FOMOD options, readme files, etc.)
into a zip — useful for inspecting, sharing, or preserving mods independent of
Limo.  
**Linux relevance:** Linux users have no Nexus desktop app fallback, so Limo is
the only tool they have to retrieve downloaded mod contents.  
**Effort:** M  
**Source:** Nexus-Mods/NexusMods.App#4034

---

### 10. Merge staged mods into one entry
**Description:** Right-click → "Merge into…" on two or more selected mods copies
their staged files into a single mod folder and removes the originals. Useful
for managing patch chains or modular packs (e.g. a 12-part texture pack) as a
single toggleable unit.  
**Linux relevance:** Limo's hard-link deployer makes merging cheaper than on
Windows (no re-copy needed); integrates naturally with the existing staging
layout.  
**Effort:** M  
**Source:** ModOrganizer2/modorganizer#518

---

## Group 4 — Diagnostics & Health

### 11. Load-order bisect tool
**Description:** A "Bisect" mode (inspired by `git bisect`) that disables half
the active plugins/mods, prompts the user to launch the game and report whether
the issue reproduced, then repeats to narrow down the culprit in O(log n) steps.
The dialog guides the user through each step and restores the full list on
finish.  
**Linux relevance:** Reproducing crashes under Proton is slow; automated
narrowing reduces the number of painful test-launches.  
**Effort:** M–L  
**Source:** ModOrganizer2/modorganizer#488

---

### 12. Guided fix for missing plugin masters
**Description:** When the health-check or LOOT identifies a plugin whose master
ESM/ESP is absent or disabled, surface a one-click "Fix" button that either
enables the master if it exists in staging or opens a Nexus search for it.  
**Linux relevance:** No platform-specific angle, but fits naturally with the
health-check panel (#50) already planned for Limo.  
**Effort:** M  
**Source:** ModOrganizer2/modorganizer#528

---

## Group 5 — Steam Deck / Embedded Linux

### 13. Steam Deck / Decky Loader integration
**Description:** Expose a Decky plugin (or at minimum a systemd/socket API) that
allows checking for mod updates and switching profiles from Game Mode without
leaving to Desktop Mode. At minimum, a small REST/socket server running
alongside Limo that a Decky plugin can query.  
**Linux relevance:** Directly Steam Deck targeted; Limo is one of the very few
mod managers that could realistically support this.  
**Effort:** L  
**Source:** Nexus-Mods/NexusMods.App#2959

---

## Group 6 — Game Detection

### 14. Show detected game install path before managing
**Description:** On the game-selection screen display the resolved installation
path Limo has auto-detected so users can confirm they are managing the correct
Steam library copy before creating deployers.  
**Linux relevance:** Linux users often have multiple Steam libraries across
different drives/mount-points; confirming the right one before setup prevents
wasted work.  
**Effort:** S  
**Source:** Nexus-Mods/NexusMods.App#2901

---

---

## Excluded as Duplicates of Existing Limo Issues

The following ideas surfaced during research but are already covered:

| External idea | Existing Limo issue |
|---|---|
| Nexus in-app browsing / search | #33 |
| Endorse mods on Nexus | #27 |
| LOOT messages in UI | #29 |
| Download queue with progress | #8 |
| Per-mod notes | #6 |
| Conflict detail view | #4 |
| Separators in deployer | #10 |
| Theme selector | #17 |
| i18n / translations | #22 |
| CLI scripting | #44 |
| MO2/Vortex import | #45 |
| Save manager | #24 |
| Collections export/import | #1, #2 |
| Nexus changelog viewer | #15 |
| Nexus thumbnails | #35 |
| Deploy dry-run / preview | #49 |
| Health-check panel | #50 |
| Restore points / snapshot | #54 |
| Portable instance export | #55 |
| Profile export/import bundle | #13 |
| Git platform update checks | #137 |
| Scheduled update checks | #47 |
| FOMOD scalable images | #89 |
| Third-party sources (Gamebanana, Thunderstore) | #60 |
| Log viewer | #20 |
| Proton prefix auto-detection | #19 |
| GOG support | #74 |
| OverlayFS deployer | #83 |
| ModFS / FUSE deployer | #110 |
| Pre/post deploy hooks | #46 |
| Status column in mod list | #77 |
| Bulk enable/disable/tag | #14 |
| CSV/Markdown export of mod list | #7 (same concept) |
| Mod install date & size columns | #18 |
| Virtual deployed file tree | #11 |
| Drag-and-drop archive install | #16 (already tracked) |

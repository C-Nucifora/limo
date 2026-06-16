# Limo — Remediation Plan (exact fixes)

Generated from the comprehensive audit. **261 findings** (11 High · 78 Medium · 172 Low), grouped by file for wave-by-wave fixing. Each item has a stable ID (`F001`–`F261`) that matches its GitHub issue. Items marked ✓ were independently confirmed by adversarial verification.

Severity legend: 🔴 High · 🟠 Medium · 🟡 Low

---

## `../limo-deps/cpr/CMakeLists.txt`

### F090 — 🟡 Low · Security · ✓verified
**Location:** `../limo-deps/cpr/CMakeLists.txt:310-311`  
**Title:** Bundled cpr 1.15.0 pins curl 8.13.0, which is affected by known CVEs (CVE-2025-5399, CVE-2026-3805)  
**Problem:** Limo links cpr 1.15.0 (see /home/christian/Documents/GitHub/limo/limo-deps/cpr/CMakeLists.txt:2 project(cpr VERSION 1.15.0)). When built without CPR_USE_SYSTEM_CURL, cpr FetchContent-pins curl 8.13.0 (CMakeLists.txt:310-311, URL curl-8_13_0 with SHA256 4a09397...). curl 8.13.0 is vulnerable to CVE-2025-5399 (WebSocket endless busy-loop DoS, introduced 8.13.0, fixed 8.14.1) and CVE-2026-3805 (SMB connection-reuse use-after-free, affects 8.13.0-8.18.0, fixed 8.19.0). Limo only issues HTTP/HTTPS requests (all cpr::Get/Post/Download targets are https URLs in src/core/nexus/api.cpp, src/core/remote/*.cpp) and cpr defaults to CPR_ENABLE_CURL_HTTP_ONLY=ON (cpr/CMakeLists.txt:63), which disables the WebSocket and SMB code paths, so neither CVE is reachable in practice. It is still an outdated, CVE-bearing dependency that ships by default and should be bumped. Note: on the current dev machine libcpr.so actually links the system curl (/usr/lib/libcurl.so.4 = 8.20.0), which is also inside the CVE-2026-3805 vulnerable range but likewise not reachable (HTTP-only).  
**Exact fix:** Bump the bundled cpr to a release that FetchContent-pins curl >= 8.19.0, or build with CPR_USE_SYSTEM_CURL=ON against a patched distro curl (>= 8.19.0). Keep CPR_ENABLE_CURL_HTTP_ONLY=ON so the WS/SMB attack surface stays disabled.

---

## `.github/workflows/ci.yml`

### F091 — 🟡 Low · Security · ✓verified
**Location:** `.github/workflows/ci.yml:24-39`  
**Title:** GitHub Actions pinned to mutable major-version tags instead of commit SHAs  
**Problem:** All third-party and first-party actions are referenced by floating major tags: actions/checkout@v4, actions/cache@v4 (ci.yml lines 24,39,75,89) and actions/upload-artifact@v4, softprops/action-gh-release@v2 (release.yml lines 39,56,155,166). Major tags are mutable; if a tag is moved to a malicious commit (especially the third-party softprops/action-gh-release, which runs in release.yml where the job has `contents: write` and the GITHUB_TOKEN), it could exfiltrate the token or tamper with releases. CI runs untrusted PR code but does not expose secrets, so the bigger exposure is the release workflow.  
**Exact fix:** Pin every `uses:` to a full commit SHA (optionally with a version comment), and enable Dependabot for github-actions to keep the SHAs updated.

### F214 — 🟡 Low · Code Quality
**Location:** `.github/workflows/ci.yml:42`  
**Title:** cpr cache key omits compiler and system libcurl version, risking stale/ABI-mismatched cache  
**Problem:** The cpr cache key `cpr-${{ runner.os }}-ubuntu-24.04-${{ env.CPR_REF }}` (lines 42, 59, 92) only varies on OS string and cpr ref. cpr is built with `-DCPR_USE_SYSTEM_CURL=ON` against the runner's libcurl and g++-14. When the ubuntu-24.04 image bumps libcurl/OpenSSL or the toolchain, the cached prefix (built against the old libcurl headers/ABI) is still restored, which can cause subtle link/runtime mismatches that are hard to diagnose and are not invalidated until CPR_REF or the literal 'ubuntu-24.04' string changes.  
**Exact fix:** Add the compiler version and `dpkg -s libcurl4-openssl-dev`/runner image label to the cache key (e.g. include `${{ runner.os }}-${{ env.ImageOS }}` and a hash of relevant `dpkg --version` output).

### F092 — 🟡 Low · Security · ✓verified
**Location:** `.github/workflows/ci.yml:47-52`  
**Title:** cpr built from a mutable upstream tag rather than a pinned commit SHA  
**Problem:** Both ci.yml (lines 47-52, 97-102) and release.yml (lines 64-69) build cpr by cloning `https://github.com/libcpr/cpr` with `--branch "$CPR_REF"` where CPR_REF is a tag (1.14.2). Git tags are mutable: a force-moved tag or a compromised upstream would let arbitrary code be compiled and, in release.yml, linked into the published binary. There is no commit-SHA pin and no signature/checksum verification of the cloned tree.  
**Exact fix:** Pin to an immutable commit SHA (clone then `git checkout <sha>` and verify, or use a submodule/FetchContent with GIT_TAG set to the full 40-char SHA). Document/verify the expected commit for the 1.14.2 release.

### F109 — 🟡 Low · Functionality
**Location:** `.github/workflows/ci.yml:70-73`  
**Title:** Test job is non-blocking, so broken/failing tests cannot gate merges  
**Problem:** The `test` job sets `continue-on-error: true` (line 73) and the comment states tree-loadorder tests are 'mid-refactor'. Combined with `ctest --output-on-failure` not using `--no-tests=error`, the job reports success even if tests fail or if zero tests are discovered (e.g. a build/registration regression that produces an empty test binary). This means test regressions and an empty ctest set are both invisible at the PR gate.  
**Exact fix:** Once the load-order tests stabilize, drop continue-on-error or split known-flaky tests behind a label so real failures gate merges; add `ctest --no-tests=error` so an empty/undiscovered test set fails the job.

---

## `.github/workflows/release.yml`

### F012 — 🟠 Medium · Security · ✓verified
**Location:** `.github/workflows/release.yml:113-118`  
**Title:** linuxdeploy downloaded from unpinned 'continuous' release with no checksum/signature verification  
**Problem:** The release job downloads linuxdeploy-x86_64.AppImage and linuxdeploy-plugin-qt-x86_64.AppImage from the GitHub 'continuous' (rolling) release tag and immediately chmod +x and executes them to package the published artifact. The 'continuous' tag is mutable and the binaries are never verified against a known checksum or signature. A compromise of the linuxdeploy repo/release, or a MITM able to serve a malicious AppImage, would result in arbitrary code running in the release pipeline and being baked into the Limo AppImage that users download. This is the published-artifact supply chain, so the blast radius is every release consumer.  
**Exact fix:** Pin to a specific linuxdeploy release tag (not 'continuous') and verify a SHA256 checksum before chmod/exec, e.g. download the matching .sha256 (or hardcode the expected hash) and run `sha256sum -c` and abort on mismatch. Optionally vendor a known-good copy.

### F110 — 🟡 Low · Functionality
**Location:** `.github/workflows/release.yml:120-141`  
**Title:** AppImage build failure silently degrades the published Release to tarball-only  
**Problem:** The 'Build AppImage' step is marked `continue-on-error: true` (line 124). If linuxdeploy fails, the job continues, the tarball is created, and the 'Publish GitHub Release' step (lines 164-175) still runs on a tag and uploads only the tarball (the `*.AppImage` glob matches nothing). The Release is published as if successful, with no AppImage and no warning, because `generate_release_notes` does not reflect missing assets. Users expecting the primary AppImage artifact get a silently incomplete release.  
**Exact fix:** After the AppImage step, gate the Release publish on the AppImage having been produced (check steps.appimage.outcome / presence of the file) and fail or annotate the run when it is missing, so a broken AppImage does not produce a silent tarball-only release.

---

## `.gitignore`

### F215 — 🟡 Low · Code Quality
**Location:** `.gitignore:74-104`  
**Title:** Build directories are inconsistently gitignored (build-off, build-clean not ignored)  
**Problem:** The repo uses several local build dirs but .gitignore covers only some: `build`, `build-fix` (via `build-fix/`), `build-qt6`, `build-repro` are ignored, but `build-off` and `build-clean` are NOT (confirmed via git check-ignore). build-off currently exists on disk (per repo listing) and could be accidentally committed in bulk `git add`, polluting the tree with large generated objects. The patterns are also a mix of bare names and trailing-slash names, which is brittle.  
**Exact fix:** Normalize to a single rule such as `build*/` (or explicitly add `build-off/` and `build-clean/`) so all local build directories are ignored consistently.

---

## `CMakeLists.txt`

### F088 — 🟠 Medium · Code Quality
**Location:** `CMakeLists.txt:519-520`  
**Title:** configure_file writes generated consts.h into the source tree and it is committed to git  
**Problem:** `configure_file(src/core/consts.h.in ${PROJECT_SOURCE_DIR}/src/core/consts.h)` writes the generated header back into the source directory (not the build directory), and the resulting src/core/consts.h is tracked in git (confirmed: `git ls-files` lists it, it is not gitignored). This means: (1) every configure run dirties the working tree and can cause spurious git diffs; (2) APP_INSTALL_PREFIX is baked from whatever LIMO_INSTALL_PREFIX the last local configure used, so a committed consts.h can ship a wrong install prefix; (3) parallel/out-of-tree builds with different prefixes race on the same file. Generated artifacts should live under the build dir.  
**Exact fix:** Generate into ${CMAKE_CURRENT_BINARY_DIR}/src/core/consts.h, add that build include dir to the target's include path, remove src/core/consts.h from version control, and gitignore it. Keep only consts.h.in tracked.

### F212 — 🟡 Low · Feature Gap
**Location:** `CMakeLists.txt:538-558`  
**Title:** AppStream metainfo.xml is only installed for Flatpak builds, missing from regular/AppImage installs  
**Problem:** The `install(FILES flatpak/io.github.limo_app.limo.metainfo.xml ...)` rule exists only inside the `if(IS_FLATPAK)` branch (lines 541-542). The non-Flatpak `else()` branch (lines 549-558) installs the desktop file, icon and changelogs.json but no metainfo/AppStream component. As a result the AppImage produced by release.yml (IS_FLATPAK defaults OFF) ships without AppStream metadata, so software centers / appstreamcli validation on the distributed binary have no component data, and the desktop entry has no associated <component>.  
**Exact fix:** Add an `install(FILES install_files/<id>.metainfo.xml DESTINATION ${LIMO_INSTALL_PREFIX}/share/metainfo)` rule to the non-Flatpak branch (provide a non-/app metainfo copy if needed).

---

## `flatpak/io.github.limo_app.limo.metainfo.xml`

### F216 — 🟡 Low · Code Quality
**Location:** `flatpak/io.github.limo_app.limo.metainfo.xml:37-44`  
**Title:** Metainfo/changelogs/version metadata is stale relative to the fork (still 1.2.2, upstream URLs)  
**Problem:** The project VERSION is 1.2.2 (CMakeLists.txt line 3) and the newest release in both flatpak metainfo.xml (line 44) and install_files/changelogs.json (line 5) is 1.2.2 dated 2025-05-03, while this fork has merged many feature PRs (#197-#228 per repo memory). The metainfo homepage/vcs/screenshot URLs all point to the upstream limo-app org (lines 37-38, 217-230). Users of fork builds see no changelog entries for the new features and the in-app changelog dialog (driven by changelogs.json) under-reports what shipped. Also note metainfo dates (2025-05-03) and changelogs.json epoch (1746262543) are consistent with each other but both predate the fork's work.  
**Exact fix:** Add release entries for the fork's versions to both metainfo.xml and changelogs.json, bump project VERSION, and update homepage/vcs/screenshot URLs to the fork if it is distributed independently.

---

## `install_files/limo.desktop`

### F206 — 🟡 Low · UI
**Location:** `install_files/limo.desktop:1-11`  
**Title:** Desktop entries lack StartupWMClass and Keywords, hurting window-to-launcher matching  
**Problem:** Neither install_files/limo.desktop nor flatpak/io.github.limo_app.limo.desktop define StartupWMClass. The Qt app's WM_CLASS will be derived from the binary/app name and may not match the desktop file id, so under some desktops/Wayland compositors the running window is not associated with its launcher icon (taskbar shows a generic icon, no pinning grouping). There are also no Keywords= entries for search. This is a polish/UX gap in the packaging metadata.  
**Exact fix:** Add `StartupWMClass=` matching the application's WM_CLASS (typically 'limo' or the QApplication applicationName) and optionally `Keywords=mod;mods;modding;LOOT;Nexus;` to both desktop files.

---

## `scripts/repro-capture.sh`

### F111 — 🟡 Low · Functionality
**Location:** `scripts/repro-capture.sh:36-60`  
**Title:** --bug flag only works when it is the first argument; shift inside for-loop is ineffective  
**Problem:** The arg parser does `for arg in "$@"` and, on `--bug`, runs `shift` (line 40). `shift` inside a `for arg in "$@"` loop does not change the loop's already-captured iteration list, and the actual --bug handling is the separate `if [[ "${1:-}" == "--bug" ]]` on line 60, which only fires when --bug is positional $1. So `scripts/repro-capture.sh --gdb --bug 95` never prints the bug steps and instead proceeds to a full instrumented build/run, contradicting the documented usage that lists `--bug <id>` as a standalone option.  
**Exact fix:** Parse arguments with a `while [[ $# -gt 0 ]]; case $1 in --bug) print_bug_steps "$2"; exit 0;; ...; esac; shift; done` loop, or detect --bug anywhere in "$@" before the main flow, rather than relying on $1.

---

## `src/core/autotag.h`

### F112 — 🟡 Low · Functionality
**Location:** `src/core/autotag.h:59`  
**Title:** AutoTag::reapplyMods/updateMods use files.at(mod) which throws if the id is absent  
**Problem:** Both template overloads call evaluator_.evaluate(files.at(mod)); std::map::at throws std::out_of_range when a mod id in the 'mods' view is not present in the precomputed 'files' map. The staging-dir overloads build the map from the same ids so are safe, but the public map-taking overloads place the burden on every caller and will throw (uncaught at several call sites) if the two arguments ever drift out of sync.  
**Exact fix:** Look up with files.find(mod) and skip (or pass an empty file list) when missing, instead of files.at(mod).

### F113 — 🟡 Low · Functionality
**Location:** `src/core/autotag.h:171`  
**Title:** readModFiles calls path.front() without checking the relative path is non-empty  
**Problem:** In readModFiles, std::string path = getRelativePath(...); then if(path.front() == '/') dereferences the first char. If getRelativePath ever returns an empty string (e.g. degenerate/edge relative path), path.front() is undefined behavior.  
**Exact fix:** Check !path.empty() before accessing path.front().

---

## `src/core/backupmanager.cpp`

### F026 — 🟠 Medium · Functionality · ✓verified
**Location:** `src/core/backupmanager.cpp:136-137`  
**Title:** setActiveBackup can lose the live target if the second rename fails  
**Problem:** setActiveBackup() renames the live target out to a backup path (line 136) and then renames the chosen backup into the target location (line 137). The two renames are not transactional: if the second rename throws (backup directory missing/permission/EXDEV), the original target has already been moved away and the function aborts, leaving the game file/dir absent from its real location. updateDirectories() reduces but does not eliminate the race (a backup can vanish between the check and the rename).  
**Exact fix:** Verify the destination backup exists immediately before swapping, and on failure of the second rename roll back the first rename (move the saved-off target back) inside a try/catch so the live target is never left missing.

### F027 — 🟠 Medium · Functionality
**Location:** `src/core/backupmanager.cpp:291`  
**Title:** std::stoi on backup-id extension can throw std::out_of_range / std::invalid_argument  
**Problem:** updateDirectories() scans sibling files matching the *.lmmbakman naming scheme and calls std::stoi(extension) on the numeric component. The code only checks that the extension contains no non-digit characters; it does not bound the length, so a crafted or corrupted filename like 'target.99999999999999999999.lmmbakman' makes std::stoi throw std::out_of_range, which propagates out of the directory-consistency routine and aborts the operation (and is invoked from many entry points). An empty numeric component would also throw std::invalid_argument.  
**Exact fix:** Use std::from_chars or wrap std::stoi in try/catch, treating un-parseable/overflowing ids as 'unknown' (move aside) rather than letting the exception escape.

### F114 — 🟡 Low · Functionality
**Location:** `src/core/backupmanager.cpp:132`  
**Title:** Swapped arguments in setActiveBackup error message  
**Problem:** The format string is "Invalid backup id: {} for target: \"{}\"" but the arguments are passed as target.target_name then backup_id, so the message prints the target name in the numeric id slot and the numeric id in the name slot. Misleading diagnostics for an out-of-range backup id.  
**Exact fix:** Swap the arguments to std::format so backup_id fills the first placeholder and target.target_name the second.

---

## `src/core/bg3deployer.cpp`

### F028 — 🟠 Medium · Functionality
**Location:** `src/core/bg3deployer.cpp:56`  
**Title:** getModNames uses .at() on uuid_map_/pak_files_ that can throw on desynced state  
**Problem:** getModNames calls pak_files_.at(uuid_map_.at(uuid)).getPluginName(uuid) for every plugin. If uuid_map_ or pak_files_ does not contain the entry (e.g. after a parse failure left plugins_ referencing a removed pak, or loadSettingsPrivate skipped an unparseable cached pak via the catch at lines 380-389 while plugins_ persisted), .at() throws std::out_of_range. Because this is called from UI code to list mods, an uncaught throw can crash the application on a malformed/stale config.  
**Exact fix:** Guard with contains()/find() and skip or substitute a placeholder name when the uuid is not mapped, consistent with cleanState()'s intent to keep the structures coherent.

### F029 — 🟠 Medium · Functionality
**Location:** `src/core/bg3deployer.cpp:74`  
**Title:** getModConflicts indexes plugins_/uuid_map_/pak_files_ without bounds or membership checks  
**Problem:** getModConflicts uses plugins_[mod_id] (line 74) with no check that mod_id is within [0, plugins_.size()), then pak_files_[uuid_map_[plugin_uuid]] (line 75) using operator[] on std::map which silently inserts a default-constructed entry if the uuid is missing. An out-of-range mod_id reads out of bounds; a stale/inconsistent uuid (possible because cleanState and updatePluginsPrivate manipulate these maps independently and parsing failures can desync them) default-constructs a Bg3PakFile and queries it. This can crash or yield wrong conflict results on inconsistent state.  
**Exact fix:** Validate mod_id against plugins_.size() and use .find()/.contains() (or .at() with try/catch) for uuid_map_ and pak_files_ lookups, skipping or logging entries that are not present.

### F115 — 🟡 Low · Functionality
**Location:** `src/core/bg3deployer.cpp:152`  
**Title:** initPluginFile/writePluginsPrivate ignore pugixml load_file result; corrupt modsettings.lsx silently yields empty plugin set  
**Problem:** xml_doc.load_file(bg3_plugin_file_path.c_str()) (line 153, and again at 404) discards the returned xml_parse_result. If modsettings.lsx is missing or malformed, the subsequent find_child queries return empty nodes and the deployer silently treats the user's setup as having zero mods, then overwrites modsettings.lsx (line 445) with that empty/derived state, potentially discarding the player's existing mod order.  
**Exact fix:** Check the xml_parse_result; on failure log an error and abort the write rather than proceeding to overwrite modsettings.lsx with a derived-from-empty document.

---

## `src/core/bg3pakfile.cpp`

### F116 — 🟡 Low · Functionality
**Location:** `src/core/bg3pakfile.cpp:22-32`  
**Title:** JSON deserialization constructor does not validate field types/presence and may construct from stale source_file  
**Problem:** The Json::Value constructor reads source_file as asString(), modified_time as asInt64(), and iterates json_value["files"]/["plugins"] without checking that these keys exist or have the expected types. asInt loop bound `json_value["files"].size()` returns ArrayIndex (unsigned) compared against a signed int i (line 26,28) - benign for sane sizes but a type mismatch. More importantly, getTimestamp(source_path_prefix_ / source_file_) at line 24 calls sfs::last_write_time which throws if the file is missing; that exception is uncaught here (the deployer wraps Bg3PakFile(path,...) calls in try/catch but the deployer's settings-loading path at bg3deployer.cpp:378 should be confirmed to do likewise).  
**Exact fix:** Guard key access with isMember/type checks, use a matching unsigned loop index, and wrap getTimestamp/last_write_time so a missing or unreadable cached source file produces a controlled error rather than an unhandled filesystem_error.

---

## `src/core/bg3plugin.cpp`

### F117 — 🟡 Low · Functionality
**Location:** `src/core/bg3plugin.cpp:9-10`  
**Title:** pugixml load_string return value ignored when parsing untrusted meta.lsx  
**Problem:** Both the Bg3Plugin constructor (line 9-10) and isValidPlugin (line 158-159) call xml_doc.load_string(xml_string.c_str()) and ignore the returned pugi::xml_parse_result. Malformed XML extracted from an untrusted .pak is silently treated as an empty document; downstream node lookups then return empty attributes. This is not memory-unsafe (pugixml handles it), but it means parse failures on attacker-supplied data are indistinguishable from a plugin with empty fields, which can mask corrupted input. Note also xml_string_ is stored verbatim and later re-emitted into modsettings.lsx via getXmlString(), and name_/uuid_/version_/directory_ values are interpolated unescaped into XML in toXmlPluginString()/toXmlLoadorderString() (the addToXml*Node variants correctly use pugixml escaping).  
**Exact fix:** Check the parse result and reject/log on error. Prefer the addToXml*Node code paths (which let pugixml escape values) over the manual string-concatenation toXml*String builders, or XML-escape directory_/name_/uuid_/version_ before interpolation.

---

## `src/core/bsaarchive.cpp`

### F030 — 🟠 Medium · Functionality
**Location:** `src/core/bsaarchive.cpp:378-389`  
**Title:** BA2 DX10 chunk count not bounded and total_unpacked uint64 has no plausibility cap; offset never bounds-checked at parse time  
**Problem:** In parseBa2's DX10 branch, num_chunks is a uint8 (max 255) so the loop is bounded, but total_unpacked accumulates attacker-controlled uint32 unpacked sizes with no cap, and bf.offset (first_offset) is stored without any check that it lies within file_size_ (unlike parseBsa which checks rf.offset >= file_size_ at line 293). DX10 entries are marked listing_only so writeEntry refuses them (returns false), which limits impact to the displayed size, but the missing offset validation and unbounded size sum are inconsistent with the rest of the bounds-checked parser and could surface if listing_only handling ever changes.  
**Exact fix:** Validate first_offset against file_size_ during parse, and clamp/validate total_unpacked against a sane maximum (e.g. file_size_) to avoid reporting absurd sizes.

### F118 — 🟡 Low · Functionality
**Location:** `src/core/bsaarchive.cpp:59-63`  
**Title:** inflateZlib accepts Z_OK as success, silently writing a partially-decompressed buffer to disk  
**Problem:** inflateZlib treats both Z_STREAM_END and Z_OK as success. With Z_FINISH, Z_OK can be returned if the output buffer was exactly filled but the stream wasn't fully consumed, or the function can succeed while having written fewer than expected_size bytes (the trailing bytes of `output` remain zero-initialized). Since BsaArchive::writeEntry writes the full output.size() to disk regardless, a crafted compressed entry can yield a file padded with zero bytes or truncated content without any error being reported.  
**Exact fix:** Require code == Z_STREAM_END for success, and verify stream.total_out == expected_size before returning; throw on mismatch.

---

## `src/core/cryptography.cpp`

### F031 — 🟠 Medium · Functionality
**Location:** `src/core/cryptography.cpp:70`  
**Title:** Corrupt/short installation key file is silently regenerated, making stored API keys undecryptable  
**Problem:** installationKey() reads nexus_api.key and, if the stored size != 32 bytes (empty/truncated/corrupt), falls through and generates a brand-new random key, overwriting the old one. Any API key previously encrypted under the no-master-password scheme can then never be decrypted (decrypt() falls back only to the legacy default_key, not the lost installation key), silently breaking NexusMods auth with no warning. This underpins the password/key flow exercised by EnterApiPwDialog/ChangeApiPwDialog in this unit.  
**Exact fix:** Treat a wrong-size key file as an error (surface it to the user) rather than silently regenerating, or back up the old file before regenerating and warn that stored keys must be re-entered.

### F013 — 🟠 Medium · Security · ✓verified
**Location:** `src/core/cryptography.cpp:142-145`  
**Title:** Weak key derivation: master password byte-repeated to 32 bytes with no KDF or salt  
**Problem:** encrypt() (and decryptWithKey()) build the AES-256-GCM key by repeating the raw user password bytes: `key_padded[i] = actual_key[i % actual_key.size()]`. There is no salt, no PBKDF2/scrypt/Argon2, and no stretching. A short master password (e.g. 4-8 chars) is simply tiled to 32 bytes, so the effective key space equals the password's, the key bytes are periodic, and the same password always yields the same key. This makes the AES-256 label misleading and the stored API key trivially brute-forceable offline given the config file. The same pattern is duplicated in decryptWithKey() at lines 202-205.  
**Exact fix:** Derive the AES key from the master password with a salted, memory-hard KDF (e.g. PBKDF2-HMAC-SHA256 with a per-file random salt and high iteration count, or Argon2id via libsodium). Store the salt alongside the ciphertext/nonce/tag. Reject or warn on empty/very short passwords.

### F217 — 🟡 Low · Code Quality
**Location:** `src/core/cryptography.cpp:12-26`  
**Title:** throwError calls ERR_free_strings(), which deinitializes process-wide OpenSSL error strings  
**Problem:** throwError() (used on every crypto failure path) calls ERR_free_strings() after formatting the error. ERR_free_strings() frees the global error-string table for the whole process; calling it from a transient error handler can degrade later OpenSSL error reporting elsewhere in the application and is generally unnecessary with modern OpenSSL (which auto-initializes/cleans up). It is also a process-global side effect triggered by per-operation errors.  
**Exact fix:** Remove the ERR_free_strings() call from throwError(); let OpenSSL manage its own error-string lifetime, or only clear the current thread's error queue via ERR_clear_error().

### F119 — 🟡 Low · Functionality
**Location:** `src/core/cryptography.cpp:246-256`  
**Title:** decrypt() with no master password generates an installation key as a side effect of a read-only operation  
**Problem:** When no master password is set, decrypt() calls installationKey() (line 250). On a fresh install with no prior key file, installationKey() generates and persists a brand new random key just to attempt a decryption that is guaranteed to fail (there is no ciphertext encrypted under it yet), before falling back to default_key. A pure decrypt path thus has the side effect of creating persistent on-disk state, which is surprising and complicates reasoning about the fallback.  
**Exact fix:** Only consult installationKey() during decryption if the key file already exists; otherwise skip straight to the default_key fallback, avoiding key generation as a side effect of a read.

---

## `src/core/cryptography.h`

### F218 — 🟡 Low · Code Quality
**Location:** `src/core/cryptography.h:64-66, 78-79`  
**Title:** Docs claim installation key is base64-encoded, but it is stored as raw bytes  
**Problem:** The header comments state the per-installation key is "persisted base64-encoded in a file with owner-only (0600) permissions" (lines 64-66 and 78-79). The implementation in cryptography.cpp writes the raw 32 random bytes directly (`out.write(raw.data(), raw.size())`, line 101) and reads them back expecting exactly 32 bytes (line 70). No base64 is involved. The misleading doc could lead a maintainer to add base64 decoding and break key loading.  
**Exact fix:** Update the doc comments to say the key is stored as 32 raw bytes, or actually base64-encode/decode on write/read to match the documentation.

---

## `src/core/cyberpunkdeployer.cpp`

### F120 — 🟡 Low · Functionality
**Location:** `src/core/cyberpunkdeployer.cpp:95`  
**Title:** Archive load-order prefix breaks for loadorder indices > 9999 (4-digit field) silently  
**Problem:** destinationPath formats the prefix as {:04d} (line 95); for loadorder_index >= 10000 std::format simply prints more digits, so the zero-padded alphabetical ordering invariant (relied on for 'top of list wins') breaks because '10000_' sorts before '9999_'. The TODO acknowledges >9999 mods is unrealistic, but the failure is silent rather than guarded, and the same fixed width applies to Tw3Deployer's mod-folder prefix.  
**Exact fix:** Either compute the prefix width from loadorder.size() (digits = number of digits in size-1) or clamp/validate and log when the load order exceeds the fixed width, so ordering correctness is not silently violated.

### F121 — 🟡 Low · Functionality
**Location:** `src/core/cyberpunkdeployer.cpp:185`  
**Title:** deployFilesWithRemap uses copy_file without overwrite and skips hardlink/symlink on differing existing files  
**Problem:** In the copy branch sfs::copy_file(source_path, dest_path) (line 185) is called after sfs::remove(dest_path) at line 183, but if removal failed silently or a directory exists at dest, copy_file throws std::filesystem_error (no overwrite option, no try/catch), aborting the whole deployment. The base Deployer typically uses overwrite_existing; this remapped variant diverges and can throw on edge cases (e.g. dest replaced by a directory between remove and copy).  
**Exact fix:** Use sfs::copy_file(source, dest, sfs::copy_options::overwrite_existing) and wrap per-file deploy operations so a single failing file is logged and skipped rather than aborting deployment, mirroring the base class robustness.

---

## `src/core/cyberpunkredmod.cpp`

### F001 — 🔴 High · Security · ✓verified
**Location:** `src/core/cyberpunkredmod.cpp:198-205`  
**Title:** Path traversal via untrusted REDmod name when copying into game_root/mods/<name>  
**Problem:** In layoutRedMods(), the destination directory is built as `const sfs::path dest = mods_dir / mod->name;` where mod->name comes directly from the mod-controlled info.json "name" field (parseRedMod reads info["name"].asString() with no validation beyond non-empty). std::filesystem operator/ does NOT normalise or contain the right-hand operand: a name like "../../../../home/user/.config/limo" traverses out of mods_dir, and an absolute name like "/home/user/important" REPLACES the path entirely. The code then runs `sfs::remove_all(dest, ec)` followed by `sfs::copy(source_path, dest, recursive | overwrite_existing)`. A malicious mod archive can therefore cause arbitrary directory deletion (remove_all) and arbitrary file overwrite outside the intended mods directory. The same unvalidated name is also written into MODS.json (writeLoadOrderFile) and used as the mods/<name> destination, so the attack triggers during the normal 'Deploy REDmods' flow (buildRedmodDeployCommand -> layoutRedMods).  
**Exact fix:** Reject or sanitise mod->name before using it as a path component: in parseRedMod, return nullopt if the name contains a path separator ('/' or '\\'), is exactly "."/"..", is absolute, or contains ".." path elements. Alternatively, in layoutRedMods compute dest = (mods_dir / name).lexically_normal() and verify it is still under mods_dir (std::mismatch on the normalised paths) before any remove_all/copy.

### F014 — 🟠 Medium · Security · ✓verified
**Location:** `src/core/cyberpunkredmod.cpp:203-205`  
**Title:** Recursive copy of mod folder follows symlinks, allowing reads/writes outside the mod  
**Problem:** layoutRedMods() copies the mod folder with `sfs::copy(source_path, dest, sfs::copy_options::recursive | sfs::copy_options::overwrite_existing)`. Without copy_options::copy_symlinks or skip_symlinks, std::filesystem::copy follows symbolic links: a malicious or malformed mod archive that contains a symlink (e.g. pointing at /etc or at the user's home) causes the linked target's contents to be read and duplicated into the game's mods directory, and a directory symlink can redirect where files land. Combined with archives that preserve symlinks during extraction this is an information-disclosure / unexpected-write vector.  
**Exact fix:** Pass sfs::copy_options::copy_symlinks (or skip_symlinks) so symlinks are copied as links rather than followed, and/or validate that every entry under source_path resolves to a path within source_path before copying.

### F093 — 🟡 Low · Security · ✓verified
**Location:** `src/core/cyberpunkredmod.cpp:20-23`  
**Title:** quote() double-quotes paths without escaping $, backtick, backslash or embedded quotes (shell injection)  
**Problem:** quote() wraps a path in double quotes only: `return '"' + path.string() + '"';` with no escaping. Inside double quotes a POSIX shell still performs command substitution ($(...) and backticks), parameter expansion ($VAR), and backslash processing, and an embedded double-quote breaks out of the quoting entirely. The returned command string from redmodDeployCommand() is executed verbatim through popen() (mainwindow.cpp:1620 via runConcurrent -> runCommand). quote() is applied to game_root (the deployer destination directory) and redmod_exe (game_root + a fixed suffix). game_root is user-configured; a directory path containing e.g. `$(...)`, a backtick, or a double-quote yields command injection or command corruption. The TODO at line 16 explicitly acknowledges embedded quotes are unhandled. By contrast the mod-supplied -mod names correctly use shellEscape() (single-quote escaping), and Tool::quote in tool.cpp uses the safe single-quote scheme.  
**Exact fix:** Use the same POSIX single-quote escaping that shellEscape() / Tool uses for ALL interpolated values, including paths. Replace quote()'s body with the single-quote-wrapping logic (wrap in ' and replace each ' with '\'') so $, backticks, spaces, backslashes and quotes are all neutralised.

### F122 — 🟡 Low · Functionality
**Location:** `src/core/cyberpunkredmod.cpp:106-107`  
**Title:** version field silently dropped when info.json stores it as a number  
**Problem:** parseRedMod only reads version when `info["version"].isString()`. The accompanying TODO and the header note that some tooling stores version as a number. When version is a JSON number the field is silently left empty rather than converted (e.g. via asString()/numeric formatting), so UI displaying the version will show nothing for those mods.  
**Exact fix:** If info["version"] is present but not a string, fall back to info["version"].asString() (jsoncpp will stringify numbers) or explicitly handle isNumeric() to populate mod.version.

### F123 — 🟡 Low · Functionality
**Location:** `src/core/cyberpunkredmod.cpp:141-149`  
**Title:** detectRedMods ignores the error_code from directory_iterator construction  
**Problem:** detectRedMods constructs `sfs::directory_iterator(source_dir, ec)` passing ec, but never checks ec afterwards; if iteration fails (e.g. permission error mid-scan, since the range-for does not re-check ec on increment either) the function silently returns whatever it has, with no warning logged. Unlike findScriptFiles in tw3scriptmerge.cpp which logs scan errors, this fails silently, so a mod directory that cannot be read produces an empty/partial result with no diagnostic.  
**Exact fix:** Check ec after constructing the iterator and log a warning, and consider using the noexcept increment form with explicit ec checks per entry as findScriptFiles does.

---

## `src/core/cyberpunksetup.cpp`

### F219 — 🟡 Low · Code Quality
**Location:** `src/core/cyberpunksetup.cpp:46-58`  
**Title:** deviceId() calls sfs::status then a redundant ::stat, ignoring the result of the first call  
**Problem:** deviceId() first calls sfs::status(path, ec) and returns 0 if not_found, then performs a separate ::stat(path.c_str(), &buf) to read st_dev. The first sfs::status call's result (st) is used only for the not_found check and then discarded; the device id ultimately comes solely from the second ::stat. This double-stat is redundant and there is a minor TOCTOU window between the two calls, though impact is negligible for this advisory-only cross-device check.  
**Exact fix:** Drop the sfs::status call and rely on the ::stat return value alone (it already returns 0 on failure/not-found), or keep a single stat and derive both existence and st_dev from it.

---

## `src/core/deployer.cpp`

### F032 — 🟠 Medium · Functionality
**Location:** `src/core/deployer.cpp:711-715`  
**Title:** loadDeployedFiles parses .lmmfiles JSON with no error handling — malformed file throws uncaught during deploy  
**Problem:** loadDeployedFiles opens .lmmfiles from the deployment target directory and does `file >> json_object` (line 715) with no try/catch. .lmmfiles lives in the game/target directory (on disk, user- or tool-accessible and corruptible). If it is truncated or contains invalid JSON, jsoncpp throws and the exception propagates out of deploy()/computeDeploymentPlan()/verifyDeployment()/runHealthCheck() — every one of which calls loadDeployedFiles. Contrast OverlayDeployer::loadState (overlaydeployer.cpp:148-155) which wraps the same `f >> root` in try/catch and degrades gracefully. The same unguarded parse loop at lines 721-727 also calls .asString()/.asInt() on whatever shape the JSON happens to be, with no validation that `files` is an array of objects with `path`/`mod_id`.  
**Exact fix:** Wrap the JSON read in try/catch (as loadState does), log a clear error naming the file, and either treat a corrupt record as empty or surface a recoverable error rather than letting a raw jsoncpp exception abort deployment. Validate that json_object["files"] is an array and each entry has the expected members before dereferencing.

### F124 — 🟡 Low · Functionality
**Location:** `src/core/deployer.cpp:163-172`  
**Title:** setLoadorder(Json::Value) reads child fields without type/member validation  
**Problem:** setLoadorder parses a persisted load-order JSON tree. The recursive overload (lines 142-161) reads entry["id"].asInt(), entry["status"].asBool(), entry["name"].asString(), entry["expanded"].asBool() and iterates entry["children"] based only on isMember("status")/isMember("name") checks. A settings file (which is on disk and could be corrupted or hand-edited) with a 'status' member but missing/invalid 'id' will yield asInt()==0 silently, mapping the entry to mod id 0. There is no overall try/catch around the parse, so a malformed structure where 'children' is present but not an array could cause unexpected iteration behavior. Combined with the unguarded JSON read patterns elsewhere, corrupt settings degrade silently rather than being reported.  
**Exact fix:** Validate member presence/type (isInt/isString/isArray) before extraction and skip or log entries that don't conform, so a corrupt load-order doesn't silently remap mods to id 0.

### F125 — 🟡 Low · Functionality
**Location:** `src/core/deployer.cpp:662-672`  
**Title:** Deploy step removes target then recreates link non-atomically — crash window can leave file missing  
**Problem:** deployFiles does sfs::remove(dest_path) (line 664) and only afterwards creates the copy/symlink/hardlink (lines 667-672). If the process is killed or copy_file throws between the remove and the link, the target file is gone. Backups are handled in a separate prior pass (backupOrRestoreFiles), and for files that were freshly added by a mod (no backup) the original is the staged file so re-deploy recovers it; but for in-place overwrites this is a non-atomic replace. The catch block re-throws after the remove has already happened. Using a temp-name + rename would make the swap atomic on the same filesystem.  
**Exact fix:** Create the new link/copy under a temporary name in the same directory and sfs::rename it over dest_path (atomic on a single filesystem) instead of remove-then-create, so an interrupted deploy never leaves the target absent.

### F126 — 🟡 Low · Functionality
**Location:** `src/core/deployer.cpp:1207-1245`  
**Title:** keepOrRevertFileModifications catches std::runtime_error but filesystem ops throw std::filesystem::filesystem_error subclass — works, but the recovery path can itself throw uncaught  
**Problem:** In keepOrRevertFileModifications the inner try block (lines 1222-1236) catches std::runtime_error (filesystem_error derives from it, so the catch is reached). However the recovery branch then unconditionally calls sfs::copy / sfs::read_symlink / sfs::remove (lines 1231-1235) with no further guard; if those also fail (e.g. cross-device or permission) the exception escapes and aborts the operation mid-way, having already done sfs::remove(mod_file_path) at line 1221, leaving the mod's source file deleted and neither the rename nor the copy completed — i.e. data loss of the staged mod file. There is no rollback.  
**Exact fix:** Guard the recovery filesystem calls and, on failure, restore the previously removed mod_file_path (or stage the work so the source file is only removed after the replacement is safely in place). Use std::error_code overloads where a failure should not be fatal.

---

## `src/core/deployerentry.hpp`

### F220 — 🟡 Low · Code Quality
**Location:** `src/core/deployerentry.hpp:29`  
**Title:** DeployerModInfo::enabled declared as int but used as bool everywhere  
**Problem:** DeployerModInfo::enabled is declared `int enabled` (deployerentry.hpp:29) yet is consistently assigned and read as a boolean (deployer.cpp setModStatus assigns a bool, getModStatus returns it as bool, toJson writes json_object["status"] = enabled). Using int for a boolean flag is misleading and allows non-0/1 values to silently round-trip through JSON load (setLoadorder reads entry["status"].asBool() but a hand-edited settings file could store an arbitrary int).  
**Exact fix:** Change the member type to bool to match its actual usage and the JSON 'status' boolean.

---

## `src/core/fomod/dependency.cpp`

### F127 — 🟡 Low · Functionality
**Location:** `src/core/fomod/dependency.cpp:98-104`  
**Title:** fileDependency evaluation does not constrain target path within the game directory  
**Problem:** For a file_leaf dependency, evaluate() calls `pu::pathExists(target_, target_path)` where target_ comes straight from the ModuleConfig.xml `file` attribute. Unlike parseFileList (which calls pathEscapesRoot on install sources/destinations), this existence probe applies no traversal guard, so a dependency like file="../../../../etc/passwd" lets a malicious FOMOD probe for the existence of arbitrary files outside the game/target directory (an information-disclosure oracle that can branch the install on whether a host path exists). It cannot read contents, only test existence, so impact is limited, but it is unsanitised attacker input reaching a filesystem stat.  
**Exact fix:** Normalise target_ and reject/clamp absolute paths and ".." traversal (reuse the pathEscapesRoot logic) before passing it to pathExists in the file_leaf branch.

---

## `src/core/fomod/fomodinstaller.cpp`

### F128 — 🟡 Low · Functionality
**Location:** `src/core/fomod/fomodinstaller.cpp:158-164`  
**Title:** getMetaData ignores XML parse result of info.xml (silent failure)  
**Problem:** getMetaData calls doc.load_file(...) without checking the returned pugi::xml_parse_result. A malformed or missing fomod/info.xml silently yields empty Name/Version strings (child_value returns "") with no warning logged, unlike init() which logs parse failures. This makes a corrupt/missing metadata file indistinguishable from a mod that legitimately has no name/version, and gives no diagnostic to the user.  
**Exact fix:** Capture the parse result and, when it fails (and the file exists), log a warning so the empty metadata is explainable.

### F129 — 🟡 Low · Functionality
**Location:** `src/core/fomod/fomodinstaller.cpp:276-278`  
**Title:** FOMOD file priority parsed with as_int() without range/format validation  
**Problem:** `priority = priority.as_int()` reads an attacker-controlled XML attribute. pugixml's as_int silently returns 0 for non-numeric text and clamps/wraps on out-of-range values, so a malformed priority is accepted without warning. Combined with File::operator< sorting by priority, a crafted ModuleConfig.xml can reorder which of two files sharing a destination "wins", overriding the intended install order. Low impact (only affects which mod file content lands at a colliding destination), but worth validating.  
**Exact fix:** Validate the priority attribute is a well-formed integer (e.g. via std::from_chars) and warn/ignore otherwise instead of silently defaulting to 0.

### F130 — 🟡 Low · Functionality
**Location:** `src/core/fomod/fomodinstaller.cpp:289-290`  
**Title:** parseInstallSteps dereferences visible-node children iterator without checking for emptiness  
**Problem:** When a step has a <visible> child, the code does `cur_step.dependencies = *(step.child("visible").children().begin())`. If the <visible> element exists but is empty (no child elements, e.g. `<visible></visible>` or `<visible/>`), children().begin() equals children().end() and dereferencing it yields a null pugi::xml_node. Dependency(pugi::xml_node) handles a falsy node (sets dummy_node), so this does not crash, but the step's visibility condition silently becomes "always visible" with no warning, which can surprise mod authors and may not match the intended FOMOD semantics. The guard `if(step.child("visible"))` only checks the element exists, not that it has a child condition.  
**Exact fix:** Check that `step.child("visible").first_child()` is non-empty before dereferencing, and otherwise leave the default dummy dependency (or log a warning) explicitly.

---

## `src/core/importers/mo2importer.cpp`

### F015 — 🟠 Medium · Security · ✓verified
**Location:** `src/core/importers/mo2importer.cpp:98`  
**Title:** MO2 mod name from modlist.txt joined to mods_dir without rejecting path-traversal components  
**Problem:** readModlist takes the substring after the '+'/'-' prefix as the mod 'name' with no validation, then parseProfile computes mod_path = mods_dir / name and exposes it as entry.source_path. A malicious/corrupt modlist.txt entry such as '+../../../../home/user/.ssh' makes std::filesystem::path::operator/ resolve outside mods/; if such a directory exists on disk (the only guard is sfs::is_directory(mod_path)) it is accepted and later used in mainwindow.cpp (info.local_source/current_path) as an install source. The mod 'name' is also used directly as a display/identifier. Imported MO2 configs are listed as untrusted input.  
**Exact fix:** Reject entries whose name contains path separators or '..' (or that are not a single, normal filename) before building mod_path; verify the canonical mod_path stays within the canonical mods_dir (e.g. compare lexically_normal / weakly_canonical against mods_dir).

---

## `src/core/installer.cpp`

### F221 — 🟡 Low · Code Quality
**Location:** `src/core/installer.cpp:166-170`  
**Title:** Off-by-one in temp-directory exhaustion guard leaves the last id untested  
**Problem:** The loop `do tmp_dir = ...std::to_string(tmp_id); while(pu::exists(tmp_dir) && tmp_id++ < UINT_MAX);` post-increments tmp_id inside the condition, and the subsequent check `if(tmp_id == UINT_MAX) throw`. Because of post-increment ordering and the short-circuit, the failure/wrap detection is fragile: when tmp_id reaches UINT_MAX-1 it is used, incremented to UINT_MAX, and the loop exits via the comparison being false rather than because a free dir was found, yet the code may still proceed to extract into a directory it believes is free. The same pattern is duplicated in installPatch (lines 379-385). Practically unreachable (would need ~4 billion stale temp dirs), but the guard does not robustly detect exhaustion.  
**Exact fix:** Separate the search from the increment, and after the loop verify the chosen tmp_dir does not already exist before using it; throw only when no free id was found.

### F131 — 🟡 Low · Functionality
**Location:** `src/core/installer.cpp:642-650`  
**Title:** throwCompressionError discards libarchive's actual error message  
**Problem:** throwCompressionError always throws the generic "Error during archive extraction." and the code that would include archive_error_string(source) is commented out because it "sometimes crashes". As a result every extraction failure (corrupt archive, unsupported filter, write error, secure-path rejection) surfaces with an identical opaque message, making it impossible for a user to distinguish a genuinely malformed mod archive from, e.g., a blocked path-traversal entry. The root cause of the crash (likely calling archive_error_string on the wrong/closed handle) is not addressed.  
**Exact fix:** Capture archive_error_string() into a std::string immediately while the handle is still valid (guarding against nullptr), include it in the CompressionError, and remove the dead commented block.

### F222 — 🟡 Low · Code Quality
**Location:** `src/core/installer.cpp:756`  
**Title:** No-op archive_entry_set_pathname call in extraction loop  
**Problem:** Line 756 calls `archive_entry_set_pathname(entry, archive_entry_pathname(entry))`, which reads the entry's pathname and immediately sets it back to the same value. This is a no-op and misleadingly suggests path rewriting/sanitisation is happening here, when in fact entry-name security relies entirely on the ARCHIVE_EXTRACT_SECURE_* flags set on the write-disk handle. The dead call should be removed (or, if path normalisation was intended, actually implemented).  
**Exact fix:** Delete the redundant archive_entry_set_pathname line, or replace it with a real sanitisation step if one was intended.

### F132 — 🟡 Low · Functionality
**Location:** `src/core/installer.cpp:1251-1298`  
**Title:** OMOD fwrite return value unchecked; partial writes silently produce truncated files  
**Problem:** In extractOmodArchive, `std::fwrite(payload.data() + payload_offset, 1, length, file)` does not check its return value. If the write is short (disk full, quota, I/O error), the file is silently truncated, fclose succeeds, and extracted_any is set true, so the OMOD is reported as successfully extracted while containing corrupt/partial mod files. There is no error surfaced to the user.  
**Exact fix:** Check the fwrite return count against length and log a warning / treat the entry as failed when it is short; consider not marking extracted_any for a failed write.

---

## `src/core/log.cpp`

### F133 — 🟡 Low · Functionality
**Location:** `src/core/log.cpp:35`  
**Title:** std::localtime is not thread-safe; logging runs from multiple threads  
**Problem:** getTimestamp() calls `std::localtime(&cur_time)`, which returns a pointer to a shared static std::tm and is not thread-safe. Logging is invoked from worker threads as well as the UI thread (the log buffer itself is mutex-guarded, indicating concurrent use), so concurrent getTimestamp() calls race on the internal static buffer, producing garbled timestamps or, on some libc implementations, undefined behavior.  
**Exact fix:** Use the thread-safe localtime_r(&cur_time, &tm_buf) (POSIX) into a local std::tm, or std::chrono::zoned_time / std::format with a local tm, instead of std::localtime.

### F223 — 🟡 Low · Code Quality
**Location:** `src/core/log.cpp:49, 65`  
**Title:** Signed/unsigned comparison on target_printer can wrap for a negative index  
**Problem:** writeLog() guards printer access with `Log::log_printers.size() > target_printer` (lines 49 and 65). target_printer is a signed int; size() is size_t. A negative target_printer is converted to a huge unsigned value, the comparison passes, and `log_printers[target_printer]` then indexes out of bounds. target_printer is currently always caller-supplied (defaults to 0), but nothing validates it is >= 0.  
**Exact fix:** Check `target_printer >= 0 && static_cast<std::size_t>(target_printer) < Log::log_printers.size()` before indexing.

### F134 — 🟡 Low · Functionality
**Location:** `src/core/log.cpp:57-67`  
**Title:** Log file is reopened and flushed on every single message  
**Problem:** writeLog() opens an std::ofstream in append mode, writes one line, and flushes for every log call. Under verbose/debug logging (e.g. during deployment of many files) this is an open/seek/write/flush/close per message, a measurable I/O bottleneck, and it also re-stats the path each time. The in-memory ring buffer is already maintained separately, so the file handle could be kept open.  
**Exact fix:** Keep a single std::ofstream open for the lifetime of the log file (reopened on init), or batch flushes, to avoid per-message open/close overhead.

---

## `src/core/lootdeployer.cpp`

### F009 — 🔴 High · Functionality · ✓verified
**Location:** `src/core/lootdeployer.cpp:234-235`  
**Title:** Unchecked GetPlugin() return dereferenced in sortModsByConflicts  
**Problem:** After SortPlugins() returns the sorted name list, the loop calls `const auto cur_plugin = loot_handle->GetPlugin(plugin);` and immediately dereferences it via `cur_plugin->IsLightPlugin()` (and later `cur_plugin->GetMasters()`) with no null check. GetPlugin() returns null when a plugin name was not actually loaded (e.g. the file is missing/unreadable on disk, or SortPlugins returned a synthetic/hardcoded name). This is the exact crash that was explicitly fixed in getModConflicts (line 159-160) and updatePluginTagsPrivate (line 1028) but was missed here, so a missing/malformed plugin file crashes the sort with a null dereference.  
**Exact fix:** Add `if(!cur_plugin) continue;` (or treat as Standard, mirroring updatePluginTagsPrivate) immediately after the GetPlugin() call, before any member access including GetMasters().

### F033 — 🟠 Medium · Functionality
**Location:** `src/core/lootdeployer.cpp:159`  
**Title:** getModConflicts indexes plugins_ with unvalidated mod_id  
**Problem:** `loot_handle->GetPlugin(plugins_[mod_id].first)` uses std::vector::operator[] with the caller-supplied mod_id and no bounds check. If mod_id is out of range (the deployer's plugin list can be smaller than the mod table, or a stale id is passed after plugins change) this is out-of-bounds undefined behaviour. The later loop uses `i < plugins_.size()` so it is safe, but the direct mod_id access is not.  
**Exact fix:** Guard at function entry: `if(mod_id < 0 || mod_id >= static_cast<int>(plugins_.size())) return conflicts;` before indexing plugins_[mod_id].

### F034 — 🟠 Medium · Functionality
**Location:** `src/core/lootdeployer.cpp:760-763`  
**Title:** Symlink target resolved against CWD when stamping plugin mtime  
**Problem:** In writePlugins, for file-mod-order games the code does `read_symlink(plugin_path)` and then `last_write_time(actual_path, time_point)`. read_symlink returns the link's stored target, which for a relative symlink is relative to the symlink's directory, not the process CWD. Passing that relative path to last_write_time resolves it against the current working directory, so it either throws (filesystem_error, aborting writePlugins and leaving load order half-written) or stamps the wrong file. Plugins are deployed as symlinks by Limo, making this a normal-use path.  
**Exact fix:** Resolve the symlink target relative to plugin_path.parent_path() (or use weakly_canonical/read the absolute target) before calling last_write_time, and wrap the call in a try/catch so one bad link does not abort the whole write.

### F135 — 🟡 Low · Functionality
**Location:** `src/core/lootdeployer.cpp:902`  
**Title:** Malformed .loot_tags JSON throws uncaught in readPluginTags  
**Problem:** readPluginTags does `file >> json;` on the on-disk tags file with no try/catch. JsonCpp throws on malformed input, so a corrupted or hand-edited .loot_tags file propagates an exception out of the constructor/refresh path instead of falling back to regenerating tags via updatePluginTagsPrivate(). The file is a cache that is always regenerable, so a parse failure should degrade gracefully.  
**Exact fix:** Wrap the JSON read in try/catch and on failure call updatePluginTagsPrivate() to rebuild the tags, mirroring the size-mismatch fallback already present at the end of the function.

### F224 — 🟡 Low · Code Quality
**Location:** `src/core/lootdeployer.cpp:904-910`  
**Title:** JSON array sizes compared with signed int loop counters  
**Problem:** readPluginTags iterates with `for(int i = 0; i < json.size(); i++)` and a nested `int j < json[i].size()`. Json::Value::size() returns ArrayIndex (unsigned), producing a signed/unsigned comparison; getModConflicts/updatePluginTagsPrivate similarly use `int i < plugins_.size()` against size_t. These are benign for realistic sizes but are mismatched-type comparisons that compilers warn on and could misbehave at extreme sizes.  
**Exact fix:** Use Json::ArrayIndex / std::size_t (or a range-based for) for these loop counters to avoid signed/unsigned mismatch.

### F225 — 🟡 Low · Code Quality
**Location:** `src/core/lootdeployer.cpp:923-950`  
**Title:** downloadList only percent-encodes spaces in URLs  
**Problem:** downloadList manually replaces only ' ' with '%20' before handing the URL to cpr. Any other URL-unsafe character in a user-configured LIST_URLS/PRELUDE_URL entry is passed unencoded, which can cause a silent download failure or fetch a different resource than intended. Since the URL is operator/config supplied (not arbitrary network input) this is low risk, but the partial encoding is misleading.  
**Exact fix:** Use a proper URL-encoding routine (e.g. cpr/curl escaping, or QUrl::toEncoded) for the path component instead of a single space->%20 replace, or document that callers must supply pre-encoded URLs.

### F136 — 🟡 Low · Functionality
**Location:** `src/core/lootdeployer.cpp:941-946`  
**Title:** downloadList error message reports wrong URL on prelude failure  
**Problem:** downloadList takes a `url` parameter but on failure the thrown message interpolates `LIST_URLS.at(app_type_)` (the masterlist URL) rather than the `url` that actually failed. When the prelude download (downloadList(PRELUDE_URL, "prelude.yaml")) fails, the user is told the masterlist URL could not be downloaded, which is misleading and hampers troubleshooting.  
**Exact fix:** Use the `url` parameter in the error message instead of LIST_URLS.at(app_type_).

### F137 — 🟡 Low · Functionality
**Location:** `src/core/lootdeployer.cpp:988`  
**Title:** Malformed .lmmconfig JSON throws uncaught in loadSettingsPrivate  
**Problem:** loadSettingsPrivate checks for file existence and openability and resets settings on failure, but the actual parse `file >> settings;` has no exception guard. A truncated or corrupted config file makes JsonCpp throw, propagating out instead of falling back to resetSettingsPrivate() as every other failure branch in this function does.  
**Exact fix:** Wrap `file >> settings;` in try/catch and call resetSettingsPrivate() with an early return on parse failure, consistent with the other error branches.

---

## `src/core/lspakextractor.cpp`

### F035 — 🟠 Medium · Functionality
**Location:** `src/core/lspakextractor.cpp:82`  
**Title:** Decompression output buffers sized from untrusted uncompressed_size with no global cap on file-list path  
**Problem:** extractData allocates output_buffer(uncompressed_size) for LZ4/ZSTD/ZLIB. For per-file extraction the value is capped at 1GiB (line 65), but the buffer for arbitrary mod content and the input_buffer(length) at line 82 are still attacker-sized up to ~archive size. A crafted .pak declaring a length/uncompressed_size near the cap forces large allocations (1GiB) during routine plugin scanning of every .pak in the source dir, enabling a memory-exhaustion DoS when many such archives are present.  
**Exact fix:** Lower the per-file cap to a realistic meta.lsx size (these are small XML files, e.g. a few MB), and bound total allocation across the file list; reject archives whose summed declared sizes are implausible.

### F036 — 🟠 Medium · Functionality
**Location:** `src/core/lspakextractor.cpp:111-128`  
**Title:** zlib inflateInit called twice in COMPRESSION_ZLIB branch, leaking allocated stream state  
**Problem:** The COMPRESSION_ZLIB branch calls inflateInit(&stream) at line 115, sets up the input/output pointers, then calls inflateInit(&stream) AGAIN at line 123 before inflate(). The second init re-initializes the z_stream, overwriting (and leaking) the internal state allocated by the first init without calling inflateEnd on it. This leaks zlib's internal allocation on every zlib-compressed extraction. The redundant second init also re-reads avail_in/next_in that were set, so it happens to still work, but it is a clear bug.  
**Exact fix:** Remove the second `inflateInit(&stream);` call at line 123. Initialize the stream exactly once.

### F037 — 🟠 Medium · Functionality
**Location:** `src/core/lspakextractor.cpp:123`  
**Title:** zlib stream double-initialized (inflateInit called twice), leaking zlib state  
**Problem:** In the COMPRESSION_ZLIB branch, inflateInit(&stream) is called at line 115 (checked), then avail_in/next_in are set, and inflateInit(&stream) is called AGAIN at line 123 before inflate(). The second init overwrites the first internal state, leaking the first allocation and resetting the stream so the avail_in/next_in set in between may be partially clobbered. Only the duplicate result is used. This is a resource leak and a latent decode bug on the zlib path.  
**Exact fix:** Remove the second inflateInit(&stream) call at line 123; set avail_in/next_in/avail_out/next_out after a single successful init, then call inflate().

### F016 — 🟠 Medium · Security · ✓verified
**Location:** `src/core/lspakextractor.cpp:137-139`  
**Title:** Out-of-bounds read constructing path from non-null-terminated fixed char[256] field  
**Problem:** LsPakFileListEntry::path is a packed `char path[256]` with no guaranteed NUL terminator (it is populated by reinterpret_cast directly from decompressed archive bytes in readFileList line 217-218). In getFileList, `path_list.emplace_back(f.path)` constructs a std::filesystem::path from a `const char*`, which scans until a NUL byte. A malicious .pak whose entry path bytes contain no NUL within the 256-byte field causes the string construction to read past the entry into adjacent heap memory (and potentially past the end of the decompressed `data` buffer for the last entry), an out-of-bounds read / information leak and potential crash. The same unterminated `f.path` would also be used wherever the resulting path is consumed.  
**Exact fix:** Build the path with an explicit length bound, e.g. `std::string p(entry.path, strnlen(entry.path, sizeof(entry.path)))` (or std::string(entry.path, std::find(entry.path, entry.path+256, '\0'))) before constructing the filesystem path, and validate it is non-empty.

### F226 — 🟡 Low · Code Quality
**Location:** `src/core/lspakextractor.cpp:50-56`  
**Title:** file-list size consistency check can underflow/overflow in the reported value  
**Problem:** init() computes `compressed_size + 8 != header_->file_list_size`. compressed_size is uint32 and file_list_size is uint32; the error message computes `header_->file_list_size - 8`, which underflows to a huge value if file_list_size < 8. While the offset bounds checks make a tiny file_list_size unlikely to reach here, the arithmetic is unguarded and the diagnostic message can print a nonsensical (wrapped) number.  
**Exact fix:** Validate header_->file_list_size >= 8 before subtracting, and perform the comparison in uint64_t to avoid any wraparound; fix the message to show the real expected value.

### F138 — 🟡 Low · Functionality
**Location:** `src/core/lspakextractor.cpp:124-128`  
**Title:** zlib inflate success on partial stream returns silently-truncated data  
**Problem:** After inflate(&stream, Z_NO_FLUSH), only `code < 0` is treated as an error. If inflate returns Z_OK (0) because the stream ended early or output filled before Z_STREAM_END, the function returns a buffer of uncompressed_size bytes that may be partially uninitialized/incomplete with no error. The string is then returned to callers (e.g. meta.lsx XML parsing) as if fully decompressed.  
**Exact fix:** Require code == Z_STREAM_END for success, and/or check stream.total_out == uncompressed_size; throw otherwise. Consider using Z_FINISH like inflateZlib in bsaarchive.cpp does.

### F139 — 🟡 Low · Functionality
**Location:** `src/core/lspakextractor.cpp:142-147`  
**Title:** extractFile performs no bounds check on file_id  
**Problem:** extractFile indexes `file_list_[file_id]` with operator[] and no range check. The sole current caller (bg3pakfile.cpp:143) passes an enumerate index that is in-range, so it is not currently exploitable, but the public API accepts an arbitrary int and an out-of-range or negative value would be undefined behavior (OOB read).  
**Exact fix:** Validate `file_id >= 0 && static_cast<size_t>(file_id) < file_list_.size()` and throw std::out_of_range otherwise (or use file_list_.at()).

### F140 — 🟡 Low · Functionality
**Location:** `src/core/lspakextractor.cpp:171-175`  
**Title:** num_files/compressed_size read via reinterpret_cast without verifying stream read succeeded  
**Problem:** After seeking to file_list_offset, num_files and compressed_size are read with file.read(buffer.data(), 4) and reinterpret_cast, but the stream state (file.good()/gcount) is not checked. The earlier check only guarantees >=8 bytes remain in the file by size, so in practice the reads succeed, but relying on that without a post-read check is fragile; a read failure would interpret stale/zero buffer bytes as counts.  
**Exact fix:** Check `if(!file) throw ...` after the two reads (consistent with the readLE pattern in bsaarchive.cpp), or use a checked read helper.

---

## `src/core/lspakfilelistentry.h`

### F002 — 🔴 High · Security · ✓verified
**Location:** `src/core/lspakfilelistentry.h:17`  
**Title:** Out-of-bounds read: non-NUL-terminated path[256] from untrusted .pak read as C-string  
**Problem:** LsPakFileListEntry.path is a fixed char[256] populated by reinterpret_cast directly from decompressed, attacker-controlled .pak file-list bytes (lspakextractor.cpp:217-218). It is later converted to a std::filesystem::path via `f.path` in LsPakExtractor::getFileList (lspakextractor.cpp:138, `path_list.emplace_back(f.path)`), which constructs from a const char* and reads until a NUL byte. A malicious BG3 .pak can fill all 256 bytes with no NUL, so the path constructor reads past the array into adjacent struct fields and the heap buffer, producing an out-of-bounds read (info leak / potential crash) on a mod-controlled archive parsed automatically by Bg3Deployer.  
**Exact fix:** Treat the field as bounded: construct the path/string with an explicit length, e.g. std::string(entry.path, strnlen(entry.path, sizeof(entry.path))), and reject entries whose 256 bytes contain no terminator. Do this where LsPakFileListEntry is materialized in getFileList().

---

## `src/core/mod.cpp`

### F141 — 🟡 Low · Functionality
**Location:** `src/core/mod.cpp:57`  
**Title:** size_on_disk (unsigned long) loaded from signed asInt64() without range check  
**Problem:** Mod(const Json::Value&) reads size_on_disk = json["size_on_disk"].asInt64() into an unsigned long member. A negative or oversized value in a tampered/corrupt mod metadata JSON silently becomes a huge unsigned value, which can mislead size accounting / disk-usage UI. Other integer fields (id, install_time) are likewise taken without validation, but size is the one that wraps.  
**Exact fix:** Validate that size_on_disk is non-negative (clamp to 0 on negative) and within a sane bound when deserializing.

---

## `src/core/moddedapplication.cpp`

### F003 — 🔴 High · Security · ✓verified
**Location:** `src/core/moddedapplication.cpp:1543`  
**Title:** Imported instance bundle can inject arbitrary shell commands via deploy hooks (std::system)  
**Problem:** runHook() runs the stored hook strings verbatim with std::system(command.c_str()) (line 1543). These hooks (pre_deploy_hook_/post_deploy_hook_/pre_undeploy_hook_/post_undeploy_hook_) are loaded from the on-disk config in updateState (lines 3376-3386), and exportInstance copies json_settings_ wholesale into an instance bundle (line 2152) - including the "hooks" object. parseInstanceBundle/importInstanceInto (lines 2198-2244) write that untrusted bundle config straight to lmm_mods.json with no validation or sanitization of the hooks. Consequently, importing a maliciously crafted instance bundle and then performing any deploy/undeploy (runHook invoked at lines 89/107/139/145) yields arbitrary command execution as the user. No consent gate exists in this data path.  
**Exact fix:** Do not auto-load executable hooks from imported bundles. Either strip the "hooks" (and tool command) fields during importInstanceInto/parseInstanceBundle, or require explicit user review/confirmation before any imported hook is persisted or run. Prefer running hooks via an argv exec (posix_spawn/execvp) rather than std::system, and surface the exact command to the user before first execution.

### F038 — 🟠 Medium · Functionality
**Location:** `src/core/moddedapplication.cpp:623`  
**Title:** Deployer-indexed operations dereference deployers_[deployer] without bounds checking  
**Problem:** removeDeployer (623), editDeployer (952), getModConflicts (997), getFileConflicts (875/880), sortModsByConflicts (1306-1309) and getDeployerInfo (1364+) index deployers_[deployer] / deployers_[deployer]->... with no validation that deployer is within [0, deployers_.size()). An out-of-range or stale index (e.g. after a concurrent removeDeployer, or a desynced UI selection) is undefined behavior / out-of-bounds vector access leading to a crash. removeDeployer additionally calls cleanup() on the bad index before erasing it.  
**Exact fix:** Add a bounds check (return / throw on deployer < 0 || deployer >= deployers_.size()) at the start of each deployer-id entry point, mirroring the validation already present for backup/group/profile ids.

### F039 — 🟠 Medium · Functionality
**Location:** `src/core/moddedapplication.cpp:741`  
**Title:** Deployer index parameters are never bounds-checked before indexing deployers_  
**Problem:** Many public methods index deployers_[deployer] with no range check: getLoadorder (741-744), getFileConflicts (880), editDeployer (954), removeDeployer (623-627), getModConflicts (1000), setProfile-adjacent calls, sortModsByConflicts (1308), getConflictGroups (1315), getDeployerInfo (1367), addModToDeployer (498), removeModFromDeployer (539). A stale or out-of-range deployer id (e.g. from a desynced UI, an imported config, or a removed deployer) causes operator[] out-of-bounds undefined behaviour / crash. removeBackupTarget and removeTool guard their ids but deployer methods do not.  
**Exact fix:** Add a consistent bounds check (deployer >= 0 && deployer < deployers_.size()) at the top of each deployer-indexed public method, returning/throwing a controlled error like the profile/tool/backup methods already do.

### F040 — 🟠 Medium · Functionality
**Location:** `src/core/moddedapplication.cpp:751`  
**Title:** setStagingDir moves mods/config with sfs::rename and no error handling, can lose data across filesystems  
**Problem:** setStagingDir() with move_existing=true calls sfs::rename for every mod dir and the config file using the throwing overload and no rollback. If the new staging dir is on a different filesystem, rename throws EXDEV mid-loop, leaving mods split between the old and new directory and the config possibly unmoved. staging_dir_ is then not updated, so the instance is left in an inconsistent, partially-moved state.  
**Exact fix:** Use the std::error_code overloads, detect cross-device failure and fall back to copy+remove, and perform the move transactionally (or at least roll back already-moved entries) before committing staging_dir_.

### F041 — 🟠 Medium · Functionality
**Location:** `src/core/moddedapplication.cpp:898`  
**Title:** getAppInfo indexes app_versions_[current_profile_] with no guard against an empty profile list  
**Problem:** getAppInfo() reads app_versions_[current_profile_] unconditionally. If a config is loaded whose "profiles" array is empty (updateState pushes nothing into app_versions_, and the 'Profiles are missing' check at 3148 only fires when the key is entirely absent, not when it is an empty array), app_versions_ is empty and this is out-of-bounds. setAppVersion (1977) has the same unchecked access. The ctor only auto-creates a Default profile when no config file exists, so a hand-edited/imported config with profiles:[] reaches this path.  
**Exact fix:** In updateState, reject a config whose profiles array is empty (require at least one profile), and/or guard app_versions_ accesses with a bounds check and fall back to an empty version string.

### F042 — 🟠 Medium · Functionality
**Location:** `src/core/moddedapplication.cpp:1166`  
**Title:** removeModFromGroup can erase active_group_members_ out of sync, risking later out-of-bounds in updateDeployerGroups  
**Problem:** After removal the code uses `groups_[group].size() == 1` to decide whether to erase group_map_ for the last member, then `< 2` to erase the group itself, but active_group_members_ was already reassigned to groups_[group][0] at 1101 only inside the `!empty()` branch. The interplay between these size checks and the parallel vectors (active_group_members_, group_names_, group_notes_) is fragile; if the vectors ever desync (e.g. legacy config where group_names_/group_notes_ are shorter, only guarded by the `if(group < size)` at 1172/1174 but active_group_members_ erase at 1171 is unguarded), updateDeployerGroups indexing active_group_members_[group] (3424-3428) can go out of bounds.  
**Exact fix:** Keep all per-group parallel vectors (groups_, active_group_members_, group_names_, group_notes_) strictly in lockstep with a single helper that erases a group from all of them, and assert/bounds-check sizes after load.

### F043 — 🟠 Medium · Functionality
**Location:** `src/core/moddedapplication.cpp:2208`  
**Title:** parseInstanceBundle parses JSON with no exception handling, unlike readSettings  
**Problem:** parseInstanceBundle does `file >> root;` with no try/catch. A malformed/truncated bundle throws Json::RuntimeError that propagates raw, whereas readSettings() (3066-3096) deliberately wraps parsing, preserves the file and logs a recovery message. An import of a corrupt bundle thus produces an unhandled low-level JSON exception instead of a clean ParseError/user-facing error, and importInstanceInto may leave a partially created staging directory.  
**Exact fix:** Wrap the JSON read in try/catch and rethrow as ParseError with the file name; in importInstanceInto, clean up the freshly created staging dir/tmp file on failure.

### F044 — 🟠 Medium · Functionality
**Location:** `src/core/moddedapplication.cpp:2261-2288`  
**Title:** exportProfile leaves deployers on the wrong profile if serialization throws (no RAII restore)  
**Problem:** exportProfile() temporarily switches each non-autonomous deployer to the requested profile (setProfile(profile), line 2271), reads its load order / conflict groups, then restores the previous profile (setProfile(previous_profile), line 2286). If getLoadorder()->toJson() or getConflictGroups() throws between those two calls, the restore is skipped and the deployer is left on the exported profile while current_profile_ is unchanged, corrupting in-memory state for the rest of the session. The same transient-switch-without-RAII pattern appears in updateSettings() (lines 2973-2986) and updateState()/updateDeployerGroups()/replaceMod, where a mid-loop throw leaves a deployer on a non-current profile.  
**Exact fix:** Use an RAII guard that restores the deployer's previous profile in its destructor, so any exception during serialization restores state. Apply the same pattern to the other transient setProfile loops.

### F045 — 🟠 Medium · Functionality
**Location:** `src/core/moddedapplication.cpp:3216-3222`  
**Title:** active_member read twice; isMember check applied after value already consumed  
**Problem:** In updateState() the active group member is read with groups[group]["active_member"].asInt() into active_member (line 3216), then validated, then re-read again with groups[group]["active_member"].asInt() when pushed (line 3222). The validation condition (line 3217-3219) ORs the membership test with !groups[group].isMember("active_member"), so a group entry that omits "active_member" entirely: asInt() on the missing member yields 0, the std::find for 0 in the group typically fails, but the isMember()==false term makes the whole OR true, throwing ParseError correctly only by luck of operator ordering. More importantly the value is parsed twice and the second push (line 3222) ignores the already-computed active_member, which is brittle if the JSON value were non-trivial. A duplicate/wrong active member just below membership edge cases is easy to mis-handle.  
**Exact fix:** Read "active_member" once into a local after an explicit isMember() check, validate, then push that local. Do not re-query the JSON for the same value.

### F046 — 🟠 Medium · Functionality
**Location:** `src/core/moddedapplication.cpp:3270`  
**Title:** Mixed && / == without parentheses in loadorder group filter likely misclassifies group members on load  
**Problem:** The condition `if(!group_map_.contains(mod_id) || active_group_members_[group_map_[mod_id]] == mod_id && !(deployers_[depl]->isAutonomous()))` relies on && binding tighter than ||. The intent (per surrounding code) is to add a mod only when it has no group OR it is the active member; the dangling `&& !isAutonomous()` is grouped with the == term, so for a grouped, non-active member the right operand evaluates the autonomy of an already-known-non-autonomous deployer, and the whole expression's behaviour is not what the layout implies. This can add inactive group members to the load order on load, corrupting group/deploy state.  
**Exact fix:** Add explicit parentheses to encode the intended logic, e.g. `if(!group_map_.contains(mod_id) || active_group_members_[group_map_[mod_id]] == mod_id)` (the isAutonomous branch is already excluded by the enclosing `if(!isAutonomous())`).

### F047 — 🟠 Medium · Functionality
**Location:** `src/core/moddedapplication.cpp:3777-3808`  
**Title:** Uncaught std::out_of_range from std::stol on crafted Steam app id in config/icon path  
**Problem:** updateSteamAppId() parses an unbounded digit run (\d+) from the icon path or a deployer dest_path and passes it to std::stol (lines 3787, 3794, 3805). An icon_path or deployer dest_path containing a number with more than ~19 digits (e.g. "/steam/appcache/librarycache/99999999999999999999_icon.jpg" or a dest_path ".../steamapps/compatdata/99999999999999999999") makes std::stol throw std::out_of_range. updateSteamAppId() is called from updateState() (line 3352) whenever the config has no "steam_app_id" field; updateState(true) is invoked directly by the constructor (line 44) without a try/catch, and verifyStagingDir only catches std::ios_base::failure and Json::RuntimeError (lines 1353-1357), not std::out_of_range. A malformed/imported config thus throws an unhandled exception during ModdedApplication construction. Reachable via an imported instance bundle (importInstanceInto writes attacker-controlled config to disk, which is then loaded).  
**Exact fix:** Wrap each std::stol call in try/catch (catch std::out_of_range and std::invalid_argument) and skip/clamp on failure, or use std::from_chars with explicit error handling. Also broaden verifyStagingDir / constructor to tolerate parse exceptions.

### F227 — 🟡 Low · Code Quality
**Location:** `src/core/moddedapplication.cpp:166`  
**Title:** Duplicated, fragile mod-id allocation logic in installMod and createEmptyMod  
**Problem:** The new-mod-id allocation block (max_element + while exists + numeric_limits check) is copy-pasted in installMod (166-172) and createEmptyMod (217-224). max_element compares Mod objects by operator< (assumed to compare id); if a future Mod change alters that operator the id allocation silently breaks. The duplication invites divergence between the two paths.  
**Exact fix:** Extract a single private allocateNewModId() helper used by both call sites, and make the id comparison explicit (compare m.id) rather than relying on Mod::operator<.

### F142 — 🟡 Low · Functionality
**Location:** `src/core/moddedapplication.cpp:1062`  
**Title:** verifyDeployerDirectories returns only the last failing deployer's error  
**Problem:** The loop over deployers overwrites ret on every failure (line 1070), so when multiple deployers have directory problems only the last one's code/path/message is reported. Earlier failures are silently masked, which can mislead the user into thinking only one deployer is misconfigured.  
**Exact fix:** Return on the first failure, or accumulate all failing deployers into the message so the user sees every problematic deployer.

### F143 — 🟡 Low · Functionality
**Location:** `src/core/moddedapplication.cpp:1343`  
**Title:** verifyStagingDir swallows Json::LogicError and reports success  
**Problem:** verifyStagingDir catches std::ios_base::failure (returns 1) and Json::RuntimeError (returns 2), but a malformed config that triggers Json::LogicError (or any other parse exception) is not caught here and would propagate, while a config that parses without the specific RuntimeError path may report 0 (success) even if semantically invalid. The narrow catch set means some unparseable/invalid staging configs are reported as OK, leading the caller to proceed with a broken instance.  
**Exact fix:** Catch Json::Exception (the base) / std::exception and map to the parse-error code (2), so all JSON parse/logic failures are reported consistently.

### F144 — 🟡 Low · Functionality
**Location:** `src/core/moddedapplication.cpp:1545`  
**Title:** Failing deploy hook is logged but never surfaced or aborts deployment  
**Problem:** runHook deliberately only logs a warning when a hook exits non-zero and returns the code, but deployModsFor/unDeployModsFor (lines 89/107/139/145) ignore the return value entirely. A failed pre-deploy hook (e.g. a required pre-processing step) does not stop deployment and is easy to miss in logs, so deployment can proceed in an inconsistent state with no user-visible error.  
**Exact fix:** Surface a non-zero pre-deploy/pre-undeploy hook result to the UI (or make it configurable whether a failing pre-hook aborts), rather than silently continuing.

### F145 — 🟡 Low · Functionality
**Location:** `src/core/moddedapplication.cpp:1579`  
**Title:** addBackup/removeBackup/overwriteBackup do not persist via updateSettings  
**Problem:** addBackup (1579), removeBackup (1586), setActiveBackup (1594), setBackupName (1607), setBackupTargetName (1615) and overwriteBackup (1622) mutate bak_man_ state but, unlike nearly every other mutator in this class, do not call updateSettings(true). Backup target paths are persisted from bak_man_.getTargets() in updateSettings, so backup edits made without a subsequent settings write (e.g. no later deploy) may not be reflected on disk consistently, an inconsistency vs the rest of the class.  
**Exact fix:** Confirm whether BackupManager persists independently; if not, call updateSettings(true) after these mutations for consistency with the other setters.

### F146 — 🟡 Low · Functionality
**Location:** `src/core/moddedapplication.cpp:1969`  
**Title:** deleteAllData uses throwing filesystem calls and removes download dir without error handling  
**Problem:** deleteAllData() calls sfs::remove_all / sfs::remove on each mod dir, the config file and getDownloadDir() using the throwing overloads. A single locked/permission-denied entry throws mid-iteration, aborting the cleanup and leaving the instance in a half-deleted state. Other destructive paths in this file (pruneArchives, the merge/update rollbacks) deliberately use std::error_code to be resilient.  
**Exact fix:** Use the std::error_code overloads, accumulate failures, log them, and continue so a single unremovable file does not abort the whole teardown.

### F147 — 🟡 Low · Functionality
**Location:** `src/core/moddedapplication.cpp:2138-2139`  
**Title:** exportConfiguration writes config file without checking the ofstream opened successfully  
**Problem:** exportConfiguration opens std::ofstream file(path, ...) and immediately does file << json (lines 2138-2139) without verifying file.is_open(). If the staging directory is not writable the export silently produces no file (or a truncated one) and reports success via the prior LOG_INFO line, giving the user no error. Contrast with exportInstance (line 2191) and writeSettings (line 3052) which do check is_open() and throw.  
**Exact fix:** Check file.is_open() after opening and throw/log an error if the export file cannot be created, matching exportInstance/writeSettings.

### F148 — 🟡 Low · Functionality
**Location:** `src/core/moddedapplication.cpp:2440-2442`  
**Title:** importProfile indexes saved deployers by name; duplicate deployer names silently drop one load order  
**Problem:** importProfile builds saved_by_name as std::map<std::string,const Json::Value*> keyed on deployer name (lines 2440-2442); restoreRestorePoint does the same (lines 2594-2596). If two deployers share a name (names are user-editable and not unique), the later entry overwrites the earlier in the map, so one deployer's saved load order is silently lost on import/restore with no warning. Matching by name also fails to distinguish two deployers of different type with the same name.  
**Exact fix:** Match saved deployer data by (name, type) and/or by index, and warn when multiple saved entries collide or when a deployer cannot be uniquely matched.

### F228 — 🟡 Low · Code Quality
**Location:** `src/core/moddedapplication.cpp:2463-2489`  
**Title:** Identical recursive load-order filter_node lambda duplicated between importProfile and restoreRestorePoint  
**Problem:** The std::function filter_node lambda that recursively drops load-order entries referencing uninstalled mod ids is duplicated verbatim in importProfile (lines 2463-2506) and restoreRestorePoint (lines 2613-2656), including the outer children loop. This is non-trivial logic (separator handling, status/id semantics) maintained in two places, so a fix to one can be missed in the other.  
**Exact fix:** Extract a single private helper (e.g. filterLoadorderForInstalledMods(const Json::Value&)) and call it from both importProfile and restoreRestorePoint.

### F229 — 🟡 Low · Code Quality
**Location:** `src/core/moddedapplication.cpp:2560-2566`  
**Title:** createRestorePoint trims oldest entries with O(n^2) rebuild instead of erasing the front  
**Problem:** After appending a new restore point, the trim loop (lines 2560-2566) rebuilds the entire restore_points_ array element-by-element on each iteration to drop a single front element, an O(n^2) operation. Since at most one element is ever over the cap per call (MAX_RESTORE_POINTS=20) and Json arrays support removeIndex, this is wasteful and the nested copy is harder to read than necessary. deleteRestorePoint has the same rebuild pattern (lines 2670-2677).  
**Exact fix:** Use Json::Value::removeIndex(0) (or build the trimmed array once) instead of repeatedly copying all-but-front in a loop.

### F149 — 🟡 Low · Functionality
**Location:** `src/core/moddedapplication.cpp:3263`  
**Title:** Legacy load-order array format reads id/enabled without isObject/isMember guards  
**Problem:** The backwards-compatible old-format branch (loadorder.isArray(), line 3259) reads loadorder[mod]["id"].asInt() (line 3263) and loadorder[mod]["enabled"].asBool() (line 3272) without checking that each array element is an object containing those keys. A config (or imported bundle) whose old-style loadorder array contains a non-object element or an element missing "id"/"enabled" triggers a Json::LogicError. This is documented as thrown upstream, but it aborts the whole config load on a single malformed entry rather than skipping it.  
**Exact fix:** Guard each element with isObject() and isMember("id")/isMember("enabled") and skip (with a warning) malformed entries, consistent with the tolerant filtering used in importProfile/restoreRestorePoint.

### F230 — 🟡 Low · Code Quality
**Location:** `src/core/moddedapplication.cpp:3270-3272`  
**Title:** Redundant and confusing operator-precedence condition in legacy load-order membership check  
**Problem:** The condition `!group_map_.contains(mod_id) || active_group_members_[group_map_[mod_id]] == mod_id && !(deployers_[depl]->isAutonomous())` relies on && binding tighter than ||. This code path is only reached inside `if(!deployers_[depl]->isAutonomous())` (line 3250), so the `!(deployers_[depl]->isAutonomous())` term is always true and dead. The mixed-precedence expression without parentheses is easy to misread as a bug.  
**Exact fix:** Drop the always-true isAutonomous term and add explicit parentheses around the intended grouping for clarity.

### F150 — 🟡 Low · Functionality
**Location:** `src/core/moddedapplication.cpp:3733-3748`  
**Title:** generalizeSteamPath over-tokenizes short /home paths and is not round-trip-safe for paths containing token-like substrings  
**Problem:** home_regex ((?:\/home\/.+?)|~) tokenizes any path beginning with /home/<at least one char> to $HOME$ (line 3745-3746). Because .+? is non-greedy and the trailing (?:\/.*)? is optional, even "/home/user" with no further component becomes "$HOME$", and the install/prefix regexes greedily match .*?/steamapps/common/.*? capturing through the first game directory. A literal path that already contains a substring like "$HOME$" or "$STEAM_INSTALL_PATH$" is not escaped, so a malicious or unusual real path can be misinterpreted on re-resolution. This makes exported instance bundles non-portable or mis-resolved on import in corner cases.  
**Exact fix:** Anchor the home replacement to a path boundary (replace only up to the user's actual home prefix), and escape/validate any pre-existing token markers in real paths before tokenizing; add round-trip tests.

---

## `src/core/nexus/api.cpp`

### F048 — 🟠 Medium · Functionality
**Location:** `src/core/nexus/api.cpp:55-58`  
**Title:** No request timeout on any NexusMods/Git cpr call  
**Problem:** None of the cpr::Get/Post/Delete calls set a cpr::Timeout. A hung, throttled, or slow NexusMods/GitHub/GitLab endpoint (or a redirect to a slow host) blocks the calling thread indefinitely. Several of these (searchMods, getModChangelogs, validateKey) are invoked from UI flows, so the absence of a timeout can freeze the UI thread with no recovery.  
**Exact fix:** Add a reasonable cpr::Timeout (e.g. 15-30s) to all requests in this unit.

### F049 — 🟠 Medium · Functionality
**Location:** `src/core/nexus/api.cpp:67-89`  
**Title:** trackMod/untrackMod (void overloads) ignore the HTTP response status entirely  
**Problem:** The void Api::trackMod(mod_url) and Api::untrackMod(mod_url) overloads issue the POST/DELETE but never inspect response.status_code. A 401/403/429/5xx (expired key, rate limit, server error) is silently swallowed and the caller is told nothing failed, so the UI reports success while the mod was never (un)tracked. The newer bool trackMod(domain,mod_id,bool) overload handles this correctly; these legacy overloads do not.  
**Exact fix:** Check response.status_code and throw or return a failure indication (or route these overloads through the bool overload) so callers can surface the error.

### F050 — 🟠 Medium · Functionality
**Location:** `src/core/nexus/api.cpp:413`  
**Title:** std::stoi on JSON-derived version components can throw out_of_range in changelog sort  
**Problem:** The sort lambdas in getChangelogs and getModChangelogs call std::stoi(match[1]) on numeric substrings extracted from changelog version keys returned by NexusMods. A version key with a numeric component longer than INT_MAX digits (e.g. a long build/hash number in a malformed or hostile response) makes std::stoi throw std::out_of_range. In getChangelogs this escapes as an unhandled exception; in getModChangelogs (documented as not throwing and used from UI) it likewise escapes despite the contract.  
**Exact fix:** Use std::stoll inside a try/catch (or std::from_chars) and treat unparseable components as 0, so a malformed version key degrades gracefully instead of throwing.

### F017 — 🟠 Medium · Security · ✓verified
**Location:** `src/core/nexus/api.cpp:559-566`  
**Title:** Domain captured from mod URL is interpolated into API path without validation  
**Problem:** The regex in extractDomainAndModId captures the game domain with a greedy '(.+)' and returns it unvalidated. Callers (getMod, getModFiles, getChangelogs, getDownloadUrl(mod_url,file_id)) then std::format it directly into 'https://api.nexusmods.com/v1/games/{domain}/mods/...'. The author already recognised this risk in getDownloadUrl(nxm_url) (line 311-314) and rejects anything but [a-zA-Z0-9]+, but that guard is absent here. A crafted nexusmods.com URL coming from a collection manifest or imported instance (e.g. domain containing path/query characters) can manipulate the request path/endpoint sent with the user's API key.  
**Exact fix:** Apply the same std::regex_match(domain, std::regex("[a-zA-Z0-9]+")) validation in extractDomainAndModId (or tighten the capture group to [a-zA-Z0-9]+) before returning, mirroring getDownloadUrl(nxm_url).

### F051 — 🟠 Medium · Functionality · ✓verified
**Location:** `src/core/nexus/api.cpp:559-566`  
**Title:** extractDomainAndModId uses std::stoi (throws / truncates) and returns int for a long mod id  
**Problem:** extractDomainAndModId parses the mod id with std::stoi(match[2]) and returns std::pair<std::string,int>. A mod page URL whose numeric id exceeds INT_MAX (NexusMods ids are 64-bit elsewhere; the struct stores them as long) makes std::stoi throw std::out_of_range, which is uncaught and propagates as an unhandled exception out of getMod/getModFiles/getChangelogs/getDownloadUrl. Even within int range the value is silently truncated to int before being widened back to long, corrupting the request. The URL is attacker-influenced (collection manifests, imported configs, nxm links).  
**Exact fix:** Parse with std::stoll into a long and wrap in try/catch (or std::from_chars) returning std::nullopt on failure; change the optional's second type to long to match getMod(long).

### F231 — 🟡 Low · Code Quality
**Location:** `src/core/nexus/api.cpp:106`  
**Title:** Signed/unsigned comparison in JSON iteration loops  
**Problem:** Multiple loops iterate with 'int i = 0; i < json_body.size(); i++' (lines 106, 236, 265, 394, 470, 665, and File/Mod parsing). Json::Value::size() returns an unsigned ArrayIndex, so the comparison is signed/unsigned and an array larger than INT_MAX (not realistic for Nexus, but defensively) would loop incorrectly. More importantly using int i risks truncation; prefer the unsigned type or a range-based loop.  
**Exact fix:** Use 'Json::ArrayIndex i' or range-based iteration over json_body for these loops.

### F151 — 🟡 Low · Functionality
**Location:** `src/core/nexus/api.cpp:529-543`  
**Title:** validateKey throws ParseError on malformed 200 response instead of returning empty optional  
**Problem:** validateKey returns an empty optional on non-200 (treated as 'key invalid') but throws ParseError if a 200 body fails to parse. Callers (settingsdialog.cpp) treat the optional as the validity signal; an unexpected parse exception on an otherwise-200 response is a different, uncaught failure mode that can crash the validation flow rather than simply reporting the key as not validated.  
**Exact fix:** Catch/branch the parse failure and return an empty optional (key not validated) for consistency with the non-200 path.

### F152 — 🟡 Low · Functionality
**Location:** `src/core/nexus/api.cpp:545-552`  
**Title:** getNexusPageUrl regex domain capture is unvalidated and unused result of getDownloadUrl  
**Problem:** getNexusPageUrl captures the nxm domain with '(.*)' and interpolates it into a www.nexusmods.com URL without restricting to [a-zA-Z0-9]+, unlike getDownloadUrl(nxm_url). While this only builds a display/page URL (lower impact), an nxm URL with crafted domain characters produces a misleading or malformed nexusmods.com link used as info.remote_source for subsequent API calls in initModInfo.  
**Exact fix:** Validate the captured domain against [a-zA-Z0-9]+ (consistent with the rest of the code) before constructing the page URL.

### F232 — 🟡 Low · Code Quality
**Location:** `src/core/nexus/api.cpp:626-635`  
**Title:** Inconsistent request headers: fetchModListing/getModChangelogs/getTrackedModIds bypass authHeader()  
**Problem:** authHeader() was introduced (per the comment, to satisfy Cloudflare's bot challenge for limo#220) and sets a real User-Agent plus Application-Name/Version. Several requests do not use it and instead send a bare cpr::Header with only 'apikey' (getModChangelogs line 447, getTrackedModIds line 213) or a minimal 'Limo' User-Agent (fetchModListing line 634, gitAuthHeader line 778). These can be rejected by the same bot challenge the fix targeted, causing intermittent search/changelog/tracked-mod failures.  
**Exact fix:** Route all NexusMods requests through authHeader(api_key_) so the Cloudflare-friendly User-Agent/Application headers are applied uniformly.

---

## `src/core/nexus/collection.cpp`

### F094 — 🟡 Low · Security · ✓verified
**Location:** `src/core/nexus/collection.cpp:15-20`  
**Title:** Collection::Entry::modUrl builds a nexusmods URL from unvalidated manifest domain  
**Problem:** modUrl concatenates 'https://www.nexusmods.com/' + domain + '/mods/' + id from a collection.json manifest (untrusted import). domain is taken verbatim from the manifest info.domainName / gameId with no character validation. The resulting URL is fed back into Api::extractDomainAndModId/getModFiles, compounding the unvalidated-domain path-interpolation issue above when importing a hostile collection.  
**Exact fix:** Validate game_domain against an allowlist pattern (e.g. [a-zA-Z0-9]+) when parsing the manifest in Collection::init, before it is stored on entries and turned into URLs.

---

## `src/core/nexus/integrityverifier.cpp`

### F233 — 🟡 Low · Code Quality
**Location:** `src/core/nexus/integrityverifier.cpp:147`  
**Title:** Signed/unsigned comparison iterating JSON array  
**Problem:** `for(int i = 0; i < json_body.size(); i++)` compares a signed int against Json::Value::size() which returns the unsigned Json::ArrayIndex ( arrayValue). This produces a signed/unsigned comparison warning and, for a pathologically large array (> INT_MAX entries, or any value with the sign bit set), the loop bound is interpreted incorrectly. The same pattern appears in tool.cpp:117 and tool.cpp:147-style loops elsewhere.  
**Exact fix:** Use `Json::ArrayIndex i` (or `unsigned`) for the loop counter to match json_body.size().

---

## `src/core/openmwarchivedeployer.cpp`

### F052 — 🟠 Medium · Functionality
**Location:** `src/core/openmwarchivedeployer.cpp:89-108`  
**Title:** OpenMW archive writePlugins drops fallback-archive lines when none previously existed and no anchor matches  
**Problem:** writePlugins removes all existing `fallback-archive=` lines into nothing and records archive_line at the first removed line's index. If openmw.cfg has no fallback-archive line, archive_line stays -1; the code then writes the enabled archives as `content=` lines (line 91-95) BEFORE the loop, but writes them as fallback-archive= only at the (never-matched) archive_line inside the loop. So on a config with no prior fallback-archive entry, archives are emitted as `content=` (wrong key for BSA archives) rather than `fallback-archive=`. OpenMW expects archives under fallback-archive=, so enabled BSAs are written under the wrong directive and effectively not registered as archives.  
**Exact fix:** When archive_line == -1, append the enabled archives as `fallback-archive=` lines (mirroring the OpenMwPluginDeployer content= handling at openmwplugindeployer.cpp:557-564), not as content= lines.

---

## `src/core/openmwplugindeployer.cpp`

### F053 — 🟠 Medium · Functionality
**Location:** `src/core/openmwplugindeployer.cpp:52-66`  
**Title:** OpenMW unDeploy backup is never restored due to single-file backup vs. LootDeployer two-file restore  
**Problem:** OpenMwPluginDeployer extends LootDeployer, whose restoreUndeployBackupIfExists() (lootdeployer.cpp:952) only restores when BOTH a loadorder backup AND a plugin backup exist; otherwise it deletes the lone backup. OpenMwPluginDeployer::unDeploy only creates the plugin backup ('.'+plugin_file_name_+ext) and never an app_plugin/loadorder backup. On the next deploy the `!loadorder && plugin` branch fires and simply removes the plugin backup, so the undeploy backup is silently discarded and never restored, defeating the purpose of unDeploy. (deploy() is inherited from PluginDeployer and calls restoreUndeployBackupIfExists at the start of every deploy.)  
**Exact fix:** Override restoreUndeployBackupIfExists in OpenMwPluginDeployer to use the single-file PluginDeployer semantics, or have unDeploy also back up the loadorder file expected by LootDeployer's restore logic.

### F054 — 🟠 Medium · Functionality
**Location:** `src/core/openmwplugindeployer.cpp:646-726`  
**Title:** Data= block parser corrupts a user's BEGIN-without-END marker and loses cfg content  
**Problem:** writeDataEntries strips Limo's managed block by toggling in_block at DATA_BLOCK_BEGIN_MARKER and clearing it at DATA_BLOCK_END_MARKER. If a user's openmw.cfg happens to contain the begin marker text but no matching end marker (e.g. hand-edited, or a previous crash truncated the file mid-block), in_block stays true to EOF and every subsequent line is silently dropped from the rewritten file. Because the rewrite atomically replaces openmw.cfg, this permanently deletes the trailing portion of the user's config.  
**Exact fix:** If EOF is reached while in_block is still true, treat the unterminated block as not-Limo-owned (re-include the buffered lines), or detect/log the malformed block and abort the rewrite rather than dropping content.

### F153 — 🟡 Low · Functionality
**Location:** `src/core/openmwplugindeployer.cpp:54-57`  
**Title:** Double-dot backup filename from prefixing already-hidden plugin file name  
**Problem:** plugin_file_name_ for OpenMwPluginDeployer is '.plugins.txt' (already hidden). unDeploy constructs the backup path as `"." + plugin_file_name_ + UNDEPLOY_BACKUP_EXTENSION`, yielding '..plugins.txt.undeplbak'. The base class PluginDeployer::hideFile would have avoided the double dot. Functionally it still creates a file, but the unusual '..'-prefixed name is inconsistent with the rest of the codebase and confusing when inspecting the destination directory. The same pattern appears in OpenMwArchiveDeployer::unDeploy (openmwarchivedeployer.cpp:37) with '.archives.txt'.  
**Exact fix:** Use hideFile(plugin_file_name_) + UNDEPLOY_BACKUP_EXTENSION instead of "." + plugin_file_name_ + ..., so already-hidden names are not double-prefixed.

### F154 — 🟡 Low · Functionality
**Location:** `src/core/openmwplugindeployer.cpp:176-177`  
**Title:** sortPluginsWithLoot discards computed sort result unless enable_unsafe_sorting_ is set, but still reports success  
**Problem:** sortPluginsWithLoot computes new_plugins from libloot's SortPlugins, but only assigns `plugins_ = new_plugins` when enable_unsafe_sorting_ is true; otherwise the sorted order is silently dropped while the function still returns true and logs 'Sorted N OpenMW content files using LOOT.' For OpenMW the constructor passes false,false to LootDeployer, so whether enable_unsafe_sorting_ is true here depends on the base ctor (lootdeployer.cpp:27 sets it true unconditionally). If it is ever false, the user is told sorting succeeded while load order is unchanged, a silent no-op masked as success.  
**Exact fix:** Gate the success log/return on whether the order was actually applied, or always apply the libloot result for this deployer type and remove the dead conditional; at minimum log when the result is intentionally discarded.

### F155 — 🟡 Low · Functionality
**Location:** `src/core/openmwplugindeployer.cpp:346-379`  
**Title:** initPluginFile trusts content=/groundcover= values from openmw.cfg without validating they are bare filenames  
**Problem:** initPluginFile seeds plugins_ directly from `content=` and `groundcover=` lines in openmw.cfg using regex group `(.*\.(es[pm]|...))`. The `.*` greedily captures any path/prefix (e.g. an absolute path or directory components), and that raw value becomes a plugin name later written back as `content=<value>` and matched against on-disk filenames (which are bare names from directory_iterator). A cfg containing `content=/some/dir/Mod.esp` would store a name that never matches any on-disk file and be carried in state. Low impact (config is semi-trusted, file names on disk are bare) but the parse is looser than the on-disk model assumes.  
**Exact fix:** Strip to filename only (path_utils / filesystem filename) when ingesting content=/groundcover= values, and/or anchor the captured group to disallow path separators.

### F156 — 🟡 Low · Functionality
**Location:** `src/core/openmwplugindeployer.cpp:696-699`  
**Title:** data= entries written unquoted-safe but path containing a double quote breaks openmw.cfg  
**Problem:** writeDataEntries emits `data="<path>"` with the staging directory path inserted verbatim between double quotes. collectDataEntryPaths derives the path from source_path_ / plugin (a user/mod-controlled staging location). If any path component contains a double-quote character, the emitted line terminates the quoted string early, producing a malformed/`data=` entry that OpenMW will misparse, potentially pointing the VFS at an unintended directory. Not a shell sink, but a config-injection/parse-corruption edge case.  
**Exact fix:** Escape embedded double quotes (and backslashes) in the path per OpenMW's cfg quoting rules before writing, or reject/skip paths containing quote characters with a logged warning.

### F234 — 🟡 Low · Code Quality
**Location:** `src/core/openmwplugindeployer.cpp:789-803`  
**Title:** PLOX section-header parse mishandles header line with no closing bracket  
**Problem:** parsePloxOrderRules computes the header as `line.substr(1)` when no ']' is found. For a malformed line like '[Order' (missing bracket) the whole remainder is treated as the header name and matched, which is lenient but acceptable; however a line that is just '[' yields substr(1) == empty and active_block=0, silently ignoring it. This is best-effort parsing so impact is nil, but the inconsistent handling of malformed headers is undocumented and could surprise.  
**Exact fix:** Explicitly skip/log lines beginning with '[' that lack a closing ']' rather than partially parsing them, to make rule-file errors visible to users.

---

## `src/core/overlaydeployer.cpp`

### F055 — 🟠 Medium · Functionality
**Location:** `src/core/overlaydeployer.cpp:288-306`  
**Title:** orig/ snapshot is created only when empty and never refreshed; copy may capture already-mounted overlay content  
**Problem:** The original-game snapshot into origDir() is taken only `if(sfs::is_empty(origDir()))` (line 288). After the first deploy this directory is never refreshed, so legitimate later changes to the real game files (patches, DLC) are never reflected in the overlay's bottom layer until cleanup() wipes .overlay. More seriously, the snapshot copies dest_path_ (the mount point) recursively; if a stale overlay is still mounted at dest_path_ at this moment (the unmount at lines 278-279 only runs when isMounted()/loadState() report mounted, which can desync from reality), the snapshot would copy the merged overlay view (including mod files) into orig/, permanently baking mod content into the 'original' layer.  
**Exact fix:** Verify dest_path_ is definitively not a mount point (re-check isMounted via /proc/mounts) immediately before snapshotting, and provide a way to refresh orig/ (or detect game-file changes) rather than freezing it on first deploy.

### F056 — 🟠 Medium · Functionality
**Location:** `src/core/overlaydeployer.cpp:333-340`  
**Title:** lowerdir option corrupted by paths containing ':' or ','  
**Problem:** The lowerdir option string is built by joining mod staging directories with ':' (lines 335-340) and then embedded as lowerdir='{}' inside a comma-separated -o option list (line 346). fuse-overlayfs treats ':' as the lowerdir separator and ',' as the option separator. A staging or mod-id directory path that itself contains ':' or ',' will be split into bogus lowerdirs or spill into a different option, causing the mount to fail or silently mount the wrong layers. fuse-overlayfs supports backslash-escaping these characters; the code does not escape them.  
**Exact fix:** Escape ':' and ',' (and '\\') within each individual lowerdir path before joining (per fuse-overlayfs option syntax), or validate that staging/target paths contain none of these characters and surface a clear error if they do.

### F018 — 🟠 Medium · Security · ✓verified
**Location:** `src/core/overlaydeployer.cpp:345-356`  
**Title:** Shell command built with unescaped single-quote wrapping (command injection on '-containing paths)  
**Problem:** The fuse-overlayfs mount command is assembled with std::format using bare single-quote wrapping ('{}') around fuse_overlayfs, lowerdir_opt (built from every staged mod directory path), upperDir, workDir and dest_path_, then passed straight to popen() at line 53 (FILE* pipe = popen(full_cmd.c_str(), "r")). None of the interpolated values are escaped. Any single quote in a path closes the quoting and lets the rest of the string be interpreted by /bin/sh. The staging/source and mount/dest directories are user-configured paths (ModdedApplication::addDeployer -> info.target_dir / staging_dir, ultimately from a UI text field with no character validation), so a directory whose name contains a single quote (e.g. game folder "Tom's Game") breaks out of the quoting. A `shellEscape()` helper that correctly POSIX-escapes embedded quotes already exists in tool.cpp:21 but is not used here. The same pattern recurs in doUnmount (lines 201 and 209: `fusermount3 -u -- '{}'` and `umount -l -- '{}'`).  
**Exact fix:** Replace every '{}' wrapping of a path/binary in runCommand call sites with a proper POSIX single-quote escaper (reuse/extract tool.cpp's shellEscape so each embedded ' becomes '\''). Better yet, avoid the shell entirely: use posix_spawn/execvp with an argv array so no quoting is needed at all.

### F157 — 🟡 Low · Functionality
**Location:** `src/core/overlaydeployer.cpp:163-182`  
**Title:** isMounted does exact string match on dest_path_ without canonicalization  
**Problem:** isMounted compares the raw dest_path_.string() against the second field of /proc/mounts (lines 170-178). /proc/mounts stores the canonical/absolute mount point and escapes special characters (spaces become \040, etc.). If dest_path_ is non-canonical (trailing slash, relative component, symlink, or contains a space), the exact-string compare will fail to recognize an existing mount, leading doUnmount/deploy to believe nothing is mounted and stack a second overlay on top, or fail to clean up a live mount.  
**Exact fix:** Canonicalize dest_path_ (sfs::weakly_canonical) before comparing and decode the octal escapes (\040 etc.) in the /proc/mounts mountpoint field before the equality test.

---

## `src/core/pathutils.cpp`

### F057 — 🟠 Medium · Functionality
**Location:** `src/core/pathutils.cpp:98-108`  
**Title:** getRelativePath assumes source is a prefix of target with no validation  
**Problem:** getRelativePath() computes target.string().erase(0, source.size() + sep) without verifying that source is actually a prefix of target. If a caller passes paths where source is not a prefix (e.g. differing normalization, trailing-slash mismatch, or symlink-resolved vs raw), erase() removes the wrong number of leading characters and returns a corrupted relative path, which then drives copy/move/rename destinations. The header documents the precondition but nothing enforces it.  
**Exact fix:** Assert/verify target.string().starts_with(source.string()) (after normalization) and throw or fall back to std::filesystem::relative() when the precondition does not hold.

### F158 — 🟡 Low · Functionality
**Location:** `src/core/pathutils.cpp:93-96`  
**Title:** normalizePath only converts backslashes; does not normalize '..' or duplicate separators  
**Problem:** normalizePath() replaces backslashes with forward slashes via regex but performs no lexical normalization of '..'/'.' segments or collapsing of separators. Callers that treat the result as a normalized/safe relative path get no traversal protection from this function, which is easy to misread given the name.  
**Exact fix:** Either rename to reflect that it only swaps separators, or extend it to lexically normalize and reject leading '..' segments, and document that it is not a security boundary.

---

## `src/core/plugindeployer.cpp`

### F004 — 🔴 High · Security · ✓verified
**Location:** `src/core/plugindeployer.cpp:678-689`  
**Title:** Integer overflow in TES3 master parser permits out-of-bounds read on malformed plugin  
**Problem:** In readPluginMasters' TES3 branch, the subrecord size is read as a full 4-byte uint32 (readLE(...,4)). The bounds check `if(pos + size > data_size) break;` is done in uint32 arithmetic, so a crafted plugin with a near-UINT32_MAX size makes `pos + size` wrap around to a small value, passing the check. parseMastPayload(block.data() + pos, size) then constructs a std::string reading `size` bytes from a buffer only `data_size` long, an out-of-bounds read (undefined behavior, likely crash). Plugin files come from untrusted mod archives. The TES4 branch reads size as only 2 bytes so is unaffected.  
**Exact fix:** Compare using a wider type or rearrange to avoid overflow, e.g. `if(size > data_size - pos) break;` (pos <= data_size is already guaranteed by the loop), or cast to uint64_t before the addition.

### F159 — 🟡 Low · Functionality
**Location:** `src/core/plugindeployer.cpp:410-427`  
**Title:** loadPlugins throws on missing plugin file with no caller-side handling guarantee  
**Problem:** loadPlugins throws std::runtime_error if the plugin file cannot be opened. It is called from setProfile (after an exists() check) and restoreUndeployBackupIfExists, but also from OpenMwArchiveDeployer/OpenMwPluginDeployer constructors via `if(!initPluginFile()) loadPlugins();`. initPluginFile returns false when the plugin file already exists, so loadPlugins should succeed, but if the file is removed between the exists() check and the open (TOCTOU) or has restrictive permissions, the constructor throws, which can abort deployer construction. Minor robustness gap.  
**Exact fix:** Handle the open failure gracefully in the construction path (treat as empty plugin list) rather than throwing during object construction, or document the throw contract on the constructor.

### F160 — 🟡 Low · Functionality
**Location:** `src/core/plugindeployer.cpp:474-475`  
**Title:** loadSettings does not validate num_profiles/current_profile ranges from JSON config  
**Problem:** loadSettings reads num_profiles_ and current_profile_ via asInt() from the on-disk JSON config without bounds/consistency validation. A corrupted or hand-edited .plugin_config with e.g. a negative or absurdly large num_profiles, or current_profile >= num_profiles, is accepted. Downstream, addProfile/removeProfile/setProfile/cleanup use these as loop bounds and file-suffix indices (e.g. cleanup loops i in [0,num_profiles_)), so a huge value causes many spurious remove() calls and an inconsistent current_profile can mis-target rename operations on profile files.  
**Exact fix:** After reading, validate num_profiles_ >= 1 and 0 <= current_profile_ < num_profiles_; call resetSettings() if the values are out of range.

### F095 — 🟡 Low · Security · ✓verified
**Location:** `src/core/plugindeployer.cpp:674,705`  
**Title:** Unbounded allocation from attacker-controlled data_size in plugin header parser  
**Problem:** readPluginMasters allocates `std::vector<unsigned char> block(data_size)` where data_size is a 4-byte little-endian value taken directly from the plugin file header (both TES3 and TES4 branches). A malformed/malicious plugin can declare a data_size up to ~4 GB, triggering a multi-gigabyte allocation (and a full file read) before any sanity check, causing memory exhaustion / std::bad_alloc on every health check over the mod list. findMissingMasters wraps the call in catch(...), so it degrades rather than crashes, but it is still a trivial mod-supplied DoS and wasted I/O.  
**Exact fix:** Cap data_size against a sane maximum (e.g. the actual remaining file size via file size or a fixed limit like a few MB) before allocating, and skip the plugin if it exceeds the cap.

---

## `src/core/progressnode.cpp`

### F235 — 🟡 Low · Code Quality
**Location:** `src/core/progressnode.cpp:33-43`  
**Title:** totalSteps()/setTotalSteps mismatch type (int vs uint64_t) and total_steps_ is uninitialized  
**Problem:** total_steps_ is a uint64_t but totalSteps() returns int (truncation for large step counts), and the id-based constructor does not initialize total_steps_ for non-leaf nodes (it remains indeterminate until setTotalSteps is called). advance() guards division by zero on total_steps_==0, but a node constructed and advanced without setTotalSteps reads an uninitialized member.  
**Exact fix:** Return uint64_t from totalSteps() and default-initialize total_steps_ = 0 in the class definition.

---

## `src/core/remote/gamebanana_provider.cpp`

### F161 — 🟡 Low · Functionality
**Location:** `src/core/remote/gamebanana_provider.cpp:116-140`  
**Title:** GameBanana getModInfo indexes fixed array positions without bounds/type checks  
**Problem:** getModInfo assumes the Core/Item/Data response is an array of exactly the requested fields and blindly reads data[0]..data[4] (lines 129-134). If GameBanana returns an error object, a shorter array, or null entries (which the undocumented endpoints can do), data[1].asString() etc. dereference null/missing values; asString() on a non-string JsonCpp value throws Json::LogicError. Likewise search() (line 102) calls item["_idRow"].asInt64() with no type guard. There is no validation that json_body is actually an array before indexing. These responses are attacker-influenced (untrusted host). Low severity only because the provider is not wired into the UI.  
**Exact fix:** Verify json_body.isArray() and json_body.size() >= expected before indexing; use isMember()/type checks and tolerate missing fields. Validate the GameBanana response shape before mapping.

---

## `src/core/remote/modio_provider.cpp`

### F086 — 🟠 Medium · Feature Gap
**Location:** `src/core/remote/modio_provider.cpp:203-229`  
**Title:** mod.io getDownloadUrl is a partial stub; authenticated/subscriber downloads unimplemented  
**Problem:** getDownloadUrl only returns the public binary_url embedded in the modfile object. The OAuth bearer-token flow needed for subscriber-only or restricted mods is an unimplemented TODO (lines 209-211), and getDownloadUrl throws for any file whose download.binary_url is absent (lines 217-223). For many mod.io games binary_url is gated behind authentication, so this provider will fail to download a large class of mods. requiresApiKey() returns true and the key is mandatory, yet the resulting capability is incomplete, so the UX implies a working mod.io download path that does not fully exist.  
**Exact fix:** Implement the documented mod.io OAuth token flow (or the modfile download endpoint with Authorization: Bearer) and surface a clear, user-facing message when a mod requires authentication that is not configured.

### F162 — 🟡 Low · Functionality
**Location:** `src/core/remote/modio_provider.cpp:55-106`  
**Title:** Untrusted mod.io JSON fields accessed with asInt64()/asString() without type guards (can throw uncaught)  
**Problem:** modObjectToRemoteMod/modfileToRemoteFile read attacker-controllable JSON fields without checking value type: obj["id"].asInt64() (line 58), obj["name"].asString() (59), logo["original"].asString() (68), obj["modfile"]["version"].asString() (79), obj["filesize"].asInt64() (96), etc. With JsonCpp, asInt64() on a JSON string/object/array and asString() on a numeric/object throw Json::LogicError, not ParseError/std::runtime_error. A malicious mod.io-style response (or a spoofed CDN) with mistyped fields would throw a type these methods' callers may not anticipate. The same pattern exists in thunderstoreprovider.cpp (packageToMod/versionToFile) and gamebanana_provider.cpp (search/getModInfo/getFiles). Currently low impact because the providers are unwired.  
**Exact fix:** Guard each access with isMember()+type checks (isString/isIntegral) or wrap conversions in a helper that returns a default on type mismatch, and document that callers must catch Json::LogicError. Apply consistently across all three providers.

---

## `src/core/remote/ommrepository.cpp`

### F058 — 🟠 Medium · Functionality
**Location:** `src/core/remote/ommrepository.cpp:258-279`  
**Title:** Downloaded files are never integrity-verified despite parsed size/checksum  
**Problem:** RemoteFile carries size and checksum (md5/hash) parsed from the OMM descriptor (lines 263-264, 276-277), and all RemoteSource providers parse size_bytes, but nothing in the remote flow ever compares the downloaded bytes against the advertised size or checksum. resolveDownloadUrl() returns only the URL; the size/checksum fields are silently dropped before the download (repositoriesdialog.cpp builds RemoteDownloadInfo with only file_name/url). Unlike the NexusMods path (src/core/nexus/integrityverifier.cpp), remote-provider downloads have no integrity check, so a corrupted or tampered CDN response is imported without detection. The struct comments imply integrity data is meaningful, creating a false sense of safety.  
**Exact fix:** Plumb the advertised size and checksum through RemoteDownloadInfo/ImportModInfo and verify them after download (reuse the existing integrity-verifier hashing). At minimum verify the byte count matches the advertised size and log a warning when a checksum is present but unverifiable.

### F019 — 🟠 Medium · Security · ✓verified
**Location:** `src/core/remote/ommrepository.cpp:310-335`  
**Title:** OMM download URLs accepted with no scheme allow-list (file://, ftp://, etc.)  
**Problem:** resolveUrl()/resolveDownloadUrl() return whatever URL string the untrusted OMM XML descriptor advertises. An absolute URL is detected only by the presence of "://" (line 315) and is returned verbatim; there is no allow-list restricting the scheme to http/https. A malicious or compromised repository descriptor can therefore supply file:///etc/passwd, file:///home/<user>/.config/..., ftp://, gopher://, etc. This resolved string is emitted as RemoteDownloadInfo::download_url (repositoriesdialog.cpp:272), copied into ImportModInfo::remote_download_url (mainwindow.cpp:1460), and passed straight to cpr::Download (applicationmanager.cpp:69). cpr/libcurl will happily handle file:// and other protocols, so an attacker-controlled descriptor can make Limo read arbitrary local files into the mod-import directory (and the download-path regex at applicationmanager.cpp:23 requires a '?', so behaviour for these URLs is also undefined). The descriptor is fetched over the network and fully attacker-controlled once a user adds an untrusted repository.  
**Exact fix:** Validate the resolved URL scheme before returning it: reject anything whose scheme is not http or https (case-insensitive) in resolveUrl()/resolveDownloadUrl(), logging and returning std::nullopt. Also restrict libcurl protocols explicitly (cpr Session SetVerbose/CURLOPT_PROTOCOLS_STR to HTTPS,HTTP) on the actual download call.

### F163 — 🟡 Low · Functionality
**Location:** `src/core/remote/ommrepository.cpp:98-107`  
**Title:** listPackages re-fetches the network descriptor on every call after a failed connect  
**Problem:** listPackages() calls fetchAndParse() whenever connected_ is false (lines 100-104). repositoriesdialog.cpp constructs a fresh OmmRepository and calls listPackages() on every selection/refresh (lines 217-219, and again a new repo at 256), so each UI interaction triggers a full network round-trip and XML re-parse with no caching across calls, and a transient failure forces a re-fetch each time the list is touched. For repositories with large descriptors this is an avoidable per-interaction network/parse cost and a UI stall (it runs on the UI thread under a wait cursor, blocking the event loop).  
**Exact fix:** Cache the connected repository/packages (or move the fetch off the UI thread) and avoid reconstructing OmmRepository plus re-fetching for every selection change; reuse the already-parsed packages_.

### F164 — 🟡 Low · Functionality
**Location:** `src/core/remote/ommrepository.cpp:138-177`  
**Title:** compareVersions can misorder versions via long overflow and mixed numeric/lexical components  
**Problem:** parseLong() (lines 60-70) parses a component into a long via std::from_chars; an out-of-range numeric component (e.g. a 20-digit version segment from a crafted descriptor) returns -1, and compareVersions then treats that segment as a negative number or falls back to lexical compare inconsistently (lines 166-174). Mixed cases where one side parses numerically (>=0) and the other does not fall through to a lexical string compare of the raw parts, which can order '10' before '9' or disagree with the numeric branch. The net effect is checkForUpdate() may fail to detect a newer version (silent missed update) or falsely flag one. Edge-case only, but it affects the update-detection logic users rely on.  
**Exact fix:** Compare numeric components as big integers or by (length-then-lexical) for all-digit strings, and define a single consistent ordering for mixed numeric/non-numeric parts rather than switching strategy per component.

### F096 — 🟡 Low · Security · ✓verified
**Location:** `src/core/remote/ommrepository.cpp:187-191`  
**Title:** OpenMW-repo HTTP Basic-auth credentials sent over cpr with no https/scheme enforcement  
**Problem:** OmmRepository::fetchAndParse() builds a cpr::Session from the user-supplied url_ (set in the constructor at lines 73-74, fed from repositoriesdialog.cpp:156/218/257 with config.url verbatim) and attaches HTTP Basic auth via session.SetAuth(cpr::Authentication(user_, password_, BASIC)) whenever a user/password is configured (lines 189-190). There is no check that url_ uses https://. If the user enters an http:// repository URL, cpr/libcurl will transmit the Authorization: Basic header (base64 username:password) in cleartext, and since CPR/curl follows redirects by default an https URL that 302-redirects to http could also leak it. Unlike Nexus API keys, which are AES-256-GCM encrypted (src/core/cryptography.cpp), these repo credentials get no transport protection guarantee.  
**Exact fix:** Reject or warn on non-https URLs before calling SetAuth (validate url_ starts with "https://"), and set cpr::Redirect / CURLOPT_REDIR_PROTOCOLS to https-only so credentials are never downgraded to cleartext on redirect.

### F097 — 🟡 Low · Security · ✓verified
**Location:** `src/core/remote/ommrepository.cpp:282-294`  
**Title:** OMM file name derived from URL tail is not sanitised in the provider  
**Problem:** When a file name is missing, fetchAndParse() derives it from the last path segment of the URL (lines 287-292) and stores it as RemoteFile::file_name, which becomes RemoteDownloadInfo::file_name (repositoriesdialog.cpp:270-271) and ImportModInfo::remote_file_name. The provider performs no sanitisation of this attacker-controlled string (it could contain '..', path separators after percent-decoding, or be empty). The eventual on-disk write path is re-derived and basename-guarded in applicationmanager.cpp:32-35, so exploitation is currently blocked downstream, but the provider itself emits an unsanitised, attacker-influenced file name and relies entirely on a separate component for safety. Defence-in-depth gap.  
**Exact fix:** Sanitise the derived file name in the provider: take only the basename, reject/replace path separators and '..', and fall back to the package name when the result is empty, so the value is safe regardless of the consumer.

---

## `src/core/remote/remotesourceregistry.h`

### F087 — 🟠 Medium · Feature Gap
**Location:** `src/core/remote/remotesourceregistry.h:41-117`  
**Title:** RemoteSourceRegistry and Thunderstore/GameBanana/mod.io providers are unwired dead scaffold  
**Problem:** RemoteSourceRegistry and the three RemoteSource implementations (ThunderstoreProvider, GamebananaProvider, ModioProvider) are never referenced anywhere outside src/core/remote/ (grep across src finds zero external uses). No UI or backend ever calls registry.get(), search(), getModInfo(), getFiles(), getDownloadUrl(), or setApiKey(). Only OmmRepository is actually wired (repositoriesdialog.cpp). The file headers advertise these as functioning providers ('search() is fully implemented', etc.), but the capability is unreachable from the running application, so users cannot actually browse mod.io/Thunderstore/GameBanana. This is incomplete-feature/dead-code that can mask regressions (it compiles but is never exercised or tested).  
**Exact fix:** Either wire the registry into the repositories/import UI (provider selection -> search -> install) or clearly mark the providers as experimental/disabled. Add at least smoke tests so the parse paths are exercised.

---

## `src/core/remote/thunderstoreprovider.cpp`

### F236 — 🟡 Low · Code Quality
**Location:** `src/core/remote/thunderstoreprovider.cpp:48-49`  
**Title:** Dead conditional: mod.name assigned the same value on both branches  
**Problem:** Line 49 reads `mod.name = pkg.isMember("full_name") ? pname : pname;` — both branches of the ternary evaluate to pname, so the isMember check and the conditional are meaningless and misleading (the comment claims 'display name'). This is dead/confusing code that suggests intended behaviour that was never implemented (presumably the full_name should have produced a different display string).  
**Exact fix:** Replace with `mod.name = pname;` or implement the intended distinct display-name logic. Remove the no-op conditional.

---

## `src/core/reversedeployer.cpp`

### F059 — 🟠 Medium · Functionality
**Location:** `src/core/reversedeployer.cpp:51`  
**Title:** updateManagedFiles sets progress total from stale count before recomputing it, breaking progress  
**Problem:** updateManagedFiles sets the progress total to number_of_files_in_target_ (line 45, the value from the PREVIOUS run) and only afterwards recomputes number_of_files_in_target_ via updateFilesInDir (line 46). On the first run number_of_files_in_target_ is 0 (so std::max gives 0 total steps) while updateFilesInDir advances the node per file, and on later runs the count can mismatch the actual file count, causing the progress bar to under/overshoot or appear stuck.  
**Exact fix:** Compute the file count first (or count files in dest_path_ up front) and call setTotalSteps with the actual number before iterating, rather than the previous run's cached value.

### F060 — 🟠 Medium · Functionality
**Location:** `src/core/reversedeployer.cpp:644`  
**Title:** readManagedFiles trusts JSON-derived file paths used directly in filesystem remove/link sinks  
**Problem:** readManagedFiles reads managed_files[..][..]['path'] strings (line 652) into std::filesystem::path and stores them in managed_files_. These paths later reach destructive sinks unchecked: unDeploy does sfs::remove(dest_path_ / path) (line 97), deleteFile does sfs::remove(dest_path_ / path) and getSourcePath (lines 880-881), and deployManagedFiles links into dest_path_ / path (line 844). A path containing '..' or an absolute path (e.g. '/etc/...') in an imported/edited .revdepl-managed_files.json makes operator/ resolve outside dest_path_, so deploy/undeploy can remove or link files outside the target directory.  
**Exact fix:** On load, reject or sanitize entries whose lexically_normal path escapes the base (contains '..' or is absolute), and verify dest_path_/path stays within dest_path_ before any remove/link/copy.

### F237 — 🟡 Low · Code Quality
**Location:** `src/core/reversedeployer.cpp:323`  
**Title:** Operator precedence makes getExternallyModifiedFiles condition ambiguous/likely wrong  
**Problem:** The condition `enabled && !sfs::exists(dest_path_ / path) || !sfs::exists(getSourcePath(path, deployed_profile_))` mixes && and || without parentheses (line 323). It parses as (enabled && !destExists) || !sourceExists, so a missing source file is always reported as modified regardless of 'enabled'. Whether that is intended is unclear and the lack of grouping is a readability/correctness hazard.  
**Exact fix:** Add explicit parentheses to encode the intended grouping, e.g. (enabled && !destExists) || !sourceExists, and confirm the disabled-mod case is handled as desired.

### F165 — 🟡 Low · Functionality
**Location:** `src/core/reversedeployer.cpp:464`  
**Title:** enableSeparateDirs mishandles managed-files filename comparison and nested temp path join  
**Problem:** In enableSeparateDirs, entries are skipped when their path string equals (source_path_ / managed_files_name_).string() but the ignore-list file (.revdepl-ignored_files.json lives in dest_path_, and the deployed-loadorder file) is not excluded, and the rename target is built as source_path_ / temp_path / relative_path (line 464) where temp_path is already an absolute path under source_path_, so the join discards source_path_ and may move files to an unintended location. This data-shuffling routine can misplace or drop bookkeeping files when toggling separate dirs.  
**Exact fix:** Compare only filenames against the set of internal bookkeeping files, and join using the relative temp directory name (not the absolute temp_path) so the destination stays under source_path_.

---

## `src/core/savemanager.cpp`

### F166 — 🟡 Low · Functionality
**Location:** `src/core/savemanager.cpp:91-100`  
**Title:** deleteSave uses throwing sfs::remove despite documented non-throwing intent  
**Problem:** deleteSave() checks existence with the error_code overload but then calls sfs::remove(save_path) without an error_code, so a permission/IO failure throws std::filesystem_error (the header documents this throw, but the function otherwise returns bool for failure, an inconsistent contract). A delete invoked from the UART/UI delete flow that throws will propagate as an unhandled exception unless every caller wraps it.  
**Exact fix:** Call the std::error_code overload of sfs::remove and translate failures into a false return (or document/enforce the throwing contract consistently and ensure all callers handle it).

---

## `src/core/tagconditionnode.cpp`

### F020 — 🟠 Medium · Security · ✓verified
**Location:** `src/core/tagconditionnode.cpp:109-110`  
**Title:** Unvalidated user/config regex compiled per-file enables ReDoS hang and uncaught std::regex_error  
**Problem:** When a TagCondition has use_regex=true, evaluateWithoutInversion calls std::regex_match(target, std::regex(condition_)) for every file of a mod. condition_ is the auto-tag search_string loaded from JSON config (AutoTag ctor never validates regex syntax — only the boolean expression is checked). A crafted/typoed pattern (e.g. '(a+)+$') runs against attacker-influenced file paths/names, causing catastrophic backtracking (effective DoS freezing the app during tagging). An invalid pattern throws std::regex_error, which propagates out of evaluate(); call sites in moddedapplication.cpp (e.g. addMod->updateMods at line 207, reapplyAutoTags at 1938, updateAutoTags at 1958) are not wrapped in try/catch, so the app aborts.  
**Exact fix:** Validate and compile the regex once in the TagConditionNode constructor (store a std::regex member) inside a try/catch, rejecting invalid patterns at load time with a clear error; consider a complexity/length cap or std::regex_constants to limit backtracking, and wrap evaluation in try/catch so a bad pattern degrades gracefully instead of crashing.

### F089 — 🟠 Medium · Code Quality
**Location:** `src/core/tagconditionnode.cpp:110`  
**Title:** Regex recompiled for every file instead of once per condition  
**Problem:** std::regex(condition_) is constructed inside the per-file loop in evaluateWithoutInversion, so a mod with N files recompiles the same pattern N times. Regex compilation is expensive; across many mods/files this is a significant, avoidable performance bottleneck (and amplifies the ReDoS cost above).  
**Exact fix:** Compile the std::regex once when the node is built (or memoize it) and reuse it in the loop.

### F061 — 🟠 Medium · Functionality
**Location:** `src/core/tagconditionnode.cpp:143`  
**Title:** removeEnclosingParentheses dereferences front()/back() on a possibly-empty string (UB)  
**Problem:** The while-loop condition evaluates expression.front() == '(' && expression.back() == ')' without first checking that expression is non-empty. After the inner erases (lines 155-156) the string can become empty, and on the next loop iteration front()/back() on an empty std::string is undefined behavior. It can also be reached with an empty expression via the recursive substring paths. This can crash on malformed/edge-case expressions.  
**Exact fix:** Guard the loop with !expression.empty() (e.g. while(expression.size() >= 2 && expression.front()=='(' && expression.back()==')')) and bail out safely on empty input.

### F238 — 🟡 Low · Code Quality
**Location:** `src/core/tagconditionnode.cpp:56`  
**Title:** Signed/unsigned comparisons of condition index against container size  
**Problem:** condition_index (int) is compared with conditions.size() (size_t) at line 56, and similar int-vs-size_t comparisons appear at line 203 in expressionIsValid (std::stoi result vs num_conditions int — fine) and in autotag.cpp:74 (int i < mods_.size()). These mixed-sign comparisons are correct for the values seen in practice but generate warnings and are fragile if very large condition counts occur. std::stoi on a long numeric run in the expression could also throw std::out_of_range.  
**Exact fix:** Use a consistent unsigned/size_t type for indices and bounds, and guard std::stoi against out-of-range input (or use std::from_chars).

### F239 — 🟡 Low · Code Quality
**Location:** `src/core/tagconditionnode.cpp:346`  
**Title:** operatorOrderIsValid stores wrong span length for the 'not' token  
**Problem:** For the 'not' operator the code does token_borders.emplace_back(i, i + 2) whereas all other tokens store (start, length) — here the second field is computed as an offset (i+2) rather than a length (3). The token_borders length field is only consumed for type_group tokens (lines 382-388), so this is currently latent/cosmetic, but it is inconsistent and a hazard if the borders for 'not' are ever used.  
**Exact fix:** Use emplace_back(i, 3) to store the correct length, matching the other operator/var/group entries.

---

## `src/core/tool.cpp`

### F005 — 🔴 High · Security · ✓verified
**Location:** `src/core/tool.cpp:254,262`  
**Title:** Arguments and Protontricks-arguments fields are appended to the shell command unescaped  
**Problem:** EditToolWidget collects arguments_field_ and protontricks_arguments_field_ as free text (edittoolwidget.cpp:118-122) and stores them in the Tool. Tool::getCommand() escapes executable_path_ and working_directory_ via shellEscape() but appends arguments_ (line 262) and protontricks_arguments_ (line 254) verbatim into the popen()/system() command string. A tool imported from a config/instance bundle (or any value containing ';', '$(...)', backticks) therefore yields command injection when the tool is run. The dialog presents these as simple 'arguments' fields with no warning that their content is executed as shell syntax.  
**Exact fix:** Either build and exec the argument list via execvp-style argv rather than a shell string, or tokenize and shellEscape each argument. At minimum document and validate that these fields are shell-interpreted.

### F021 — 🟠 Medium · Security · ✓verified
**Location:** `src/core/tool.cpp:408-415`  
**Title:** Environment variable NAME is interpolated into the shell command without escaping  
**Problem:** appendEnvironmentVariables() shell-escapes the variable value (`shellEscape(value)`) but concatenates the variable NAME raw: `command += variable + "=" + shellEscape(value);` (line 414). Environment variable names originate from JSON config / the edit-tool UI (Tool ctor, tool.cpp:117-120). A name containing shell metacharacters or whitespace (e.g. `FOO=bar; rm -rf $HOME #`) is injected verbatim into the command string that is later passed to popen() (mainwindow.cpp:5327 -> runConcurrent -> popen). For the flatpak branch it is also placed after `--env=` unescaped. Because Tool definitions can be shared/imported via the JSON settings file, a crafted tool entry yields command injection.  
**Exact fix:** Validate variable names against a strict allowlist (e.g. ^[A-Za-z_][A-Za-z0-9_]*$) and reject or skip non-conforming names, or escape the entire NAME=value token rather than only the value.

### F098 — 🟡 Low · Security · ✓verified
**Location:** `src/core/tool.cpp:253-254, 261-262`  
**Title:** arguments_ and protontricks_arguments_ are concatenated into the shell command unescaped while the executable path is escaped  
**Problem:** getCommand() escapes the executable path (`shellEscape(executable_path_.string())`, line 259) and env values, but appends `arguments_` (line 262) and `protontricks_arguments_` (line 254) raw. This is intentional so a user can pass multiple flags, but the inconsistency means any data that ends up in these fields is fully shell-evaluated. Combined with the JSON-driven Tool constructor (no validation), an imported/shared tool config controls a raw fragment of the popen() command line. This is lower severity than the variable-name issue because it is the documented author-controlled freeform field, but it deserves an explicit note since the surrounding code escapes everything else.  
**Exact fix:** Document clearly (in tool.h and the edit-tool UI) that these fields are passed to the shell verbatim and must only ever contain trusted, user-authored content; never populate them from mod- or network-derived data, and warn when importing tool configs from untrusted sources.

---

## `src/core/treeitem.t.hpp`

### F062 — 🟠 Medium · Functionality
**Location:** `src/core/treeitem.t.hpp:208-214`  
**Title:** TreeItem::erase dereferences end() iterator and a possibly-expired weak_ptr when item not found  
**Problem:** erase() searches traversal_cache and then unconditionally does (*found).lock()->parent()->remove(*found) without checking that 'found' != end(). If the requested item is not present (already removed, or a stale weak_ptr), 'found' is the end iterator and dereferencing it is undefined behaviour / crash. The lambda also calls e.lock()->getData() without checking the lock succeeded, dereferencing a potentially-null shared_ptr from an expired weak entry.  
**Exact fix:** Check found != traversal_cache.end() before dereferencing and guard each weak_ptr lock() before calling getData()/parent().

---

## `src/core/tw3deployer.cpp`

### F167 — 🟡 Low · Functionality
**Location:** `src/core/tw3deployer.cpp:74`  
**Title:** rewriteTopLevelModFolder assumes first component length >= 3 ('mod') before substr  
**Problem:** After confirming the lowercased first component starts_with('mod'), the code does first_component.substr(3) (line 74). starts_with('mod') guarantees length >= 3 so this specific call is safe, but the function then reassembles paths assuming the prefix logic; a component exactly 'mod' yields remainder '' producing 'modNNNN_' which is fine, however there is no validation that the rewritten name is still a valid single path component, and unusual Unicode/whitespace folder names from a mod archive are passed through unmodified into the deployed folder name. Low risk but worth a guard.  
**Exact fix:** Keep the starts_with guard (already correct) but add a comment/assert on the length invariant, and consider normalizing/validating the resulting component to avoid surprising on-disk folder names.

---

## `src/core/tw3mergeutil.cpp`

### F168 — 🟡 Low · Functionality
**Location:** `src/core/tw3mergeutil.cpp:139-161`  
**Title:** Unmatched LIMO_MERGE_BEGIN in a hand-edited input.xml deletes to end of parent  
**Problem:** stripMergeRegions deletes every sibling from a BEGIN marker through the matching END marker. If the END marker is missing (file truncated or hand-edited so begin/end live in different parents, a case the function's own TODO at line 120 calls out), the inner while loop never sets closed=true and deletes every remaining sibling in that parent before stopping. Because stripping only runs for the incoming mod ids this is bounded to Limo's own markers, but a corrupted marker pair can still silently remove legitimate trailing content from the target input.xml.  
**Exact fix:** Before deleting, scan ahead for a matching END marker; if none exists, delete only the stray BEGIN comment (as the doc comment claims is intended) instead of the whole tail. The settings-file path has the same latent issue (lines 549-591) and warrants the same guard.

### F169 — 🟡 Low · Functionality
**Location:** `src/core/tw3mergeutil.cpp:260-275`  
**Title:** findOrCreateContext hardcodes the "context" attribute name, which the code itself flags as unverified  
**Problem:** When a fragment's InputContext has no matching context in the target, findOrCreateContext creates one and always names the identifying attribute "context" (line 272), while inputContextName() probes four possible attribute names ("context","name","Context","Name"). If the real TW3 format uses a different attribute (the inputContextName TODO at line 222 admits the identifying attribute is unverified), newly-created contexts will carry the wrong attribute name and the game may ignore them, silently dropping the merged keybinds. This is part of a broad set of unvalidated format assumptions flagged throughout the module.  
**Exact fix:** Resolve the correct identifying attribute against a real input.xml before relying on this in production; until then, surface a clear 'experimental/unvalidated format' warning in the merge result (the caller does add an experimental note, but the specific risk of created-from-scratch contexts being ignored is not communicated).

### F170 — 🟡 Low · Functionality
**Location:** `src/core/tw3mergeutil.cpp:325-346`  
**Title:** findFragment only searches depth 0 and 1, missing deeper or game-mirrored mod layouts  
**Problem:** findFragment looks for the fragment only directly under source_path and one directory level deep. The header TODO (tw3mergeutil.h:168) acknowledges real mods may nest config files arbitrarily deep or under a bin/config/... subtree mirroring the game layout. For such mods mergeInputXml/mergeSettingsFile silently find no fragment and contribute nothing, with no warning surfaced, so the user gets a 'merged 0 entries' result and no indication their mod was structured unexpectedly.  
**Exact fix:** Either perform a bounded recursive search for the fragment file or, when no fragment is found for a source whose directory clearly contains config-like files, log a warning so the silent no-op is diagnosable. At minimum document the depth limit in the user-facing merge result.

---

## `src/core/tw3scriptmerge.cpp`

### F240 — 🟡 Low · Code Quality
**Location:** `src/core/tw3scriptmerge.cpp:60-70`  
**Title:** O(n*m) full LCS table can exhaust memory on a maliciously large script pair  
**Problem:** computeMatches allocates a full (n+1)*(m+1) std::size_t matrix. For two mods shipping a pathologically large generated .ws file (e.g. hundreds of thousands of lines each), this is tens of gigabytes and will OOM/abort the process. The code's own TODO (lines 43-46) notes this. Since the inputs are mod-controlled file contents, an adversarial or accidental huge file is a denial-of-service / crash vector during 'Merge TW3 scripts'.  
**Exact fix:** Cap the line counts (skip merging and report a conflict when either file exceeds a sane threshold) or switch to the linear-space Hirschberg / Myers diff suggested in the TODO.

### F099 — 🟡 Low · Security · ✓verified
**Location:** `src/core/tw3scriptmerge.cpp:328-363`  
**Title:** recursive_directory_iterator over mod scripts follows symlinks by default  
**Problem:** findScriptFiles uses recursive_directory_iterator with only skip_permission_denied. The default does not enable follow_directory_symlink, so directory symlinks are not descended; however symlinked .ws files (regular-file symlinks) ARE reported and later read via readFileBytes, and the relative key is computed from the symlink's path. A crafted mod could place a symlinked .ws pointing outside the mod, causing its content to be read and folded into a merged output written under the staging dir. Risk is limited (output stays under staging, content only mixes into a merged script) but it is an unvalidated read of attacker-influenced link targets.  
**Exact fix:** Skip entries whose status is a symlink, or verify it->path() (resolved) stays within scripts_root before reading; reject .ws files that resolve outside the mod root.

---

## `src/core/versionchangelog.cpp`

### F241 — 🟡 Low · Code Quality
**Location:** `src/core/versionchangelog.cpp:42`  
**Title:** std::localtime is not thread-safe and its result is used unguarded  
**Problem:** versionAndDateString() calls std::localtime(&date_), which returns a pointer to a shared static std::tm and is not reentrant; concurrent use elsewhere can corrupt the formatted date. date_ is also a std::time_t populated from json["date"].asInt64() with no range validation, so a bogus changelog value yields undefined put_time output.  
**Exact fix:** Use localtime_r (or std::chrono/std::format date formatting) into a local std::tm, and validate the timestamp range before formatting.

---

## `src/core/wabbajackmodlist.cpp`

### F171 — 🟡 Low · Functionality
**Location:** `src/core/wabbajackmodlist.cpp:185`  
**Title:** Negative or oversized archive Size wraps to a huge unsigned value  
**Problem:** archive.size = static_cast<unsigned long long>(readInt(entry, {"Size"}, 0)). readInt can return a negative long long (from a negative JSON integer or a negative numeric string via std::stoll); casting to unsigned long long wraps it to a near-UINT64_MAX value, which is then shown to the user as a nonsensical file size. Untrusted .wabbajack JSON controls this field.  
**Exact fix:** Clamp negative results to 0 (or validate range) before the unsigned cast, e.g. size = readInt(...) > 0 ? static_cast<unsigned long long>(...) : 0.

---

## `src/main.cpp`

### F172 — 🟡 Low · Functionality
**Location:** `src/main.cpp:1008-1009`  
**Title:** Quote stripping on positional URL argument removes only one quote and is order-dependent  
**Problem:** When handling the positional nxm argument, only a single leading and single trailing double-quote are stripped. Inputs with mismatched or multiple quotes are passed through partially-stripped, and the stripped string is then matched against the loose nxm regex. Minor robustness issue for shell/protocol-handler-quoted arguments.  
**Exact fix:** Trim balanced surrounding quotes (or none) rather than unconditionally erasing one char from each end, and validate the result strictly.

### F173 — 🟡 Low · Functionality
**Location:** `src/main.cpp:1141`  
**Title:** nxm:// URL regex is malformed and loose, weakening URL validation  
**Problem:** The handoff regex R"(nxm:\/\/.*\mods\/\d+\/files\/\d+\?.*)" contains '\m', an invalid/again-undefined escape that std::regex treats as a literal 'm' rather than a separator boundary, and uses '.*' liberally so the validation barely constrains the URL before it is forwarded over IPC to a running instance. While the receiver should re-validate, this is the only filter applied to an externally-supplied argument (registered nxm: protocol handler) before sendString().  
**Exact fix:** Use a precise regex (escape the literal slash correctly, anchor host/game/mods/files segments) and reject anything that does not strictly match the nxm scheme before forwarding.

---

## `src/ui/addapikeydialog.cpp`

### F207 — 🟡 Low · UI
**Location:** `src/ui/addapikeydialog.cpp:23`  
**Title:** No validation on API key field; empty/malformed key can be accepted and stored  
**Problem:** getApiKey() returns ui->key_field->text() with no non-empty or format check, and the OK button is only gated on password validity (onPasswordValidityChanged). A user can confirm the dialog with an empty or malformed key, which is then encrypted and persisted, later causing opaque NexusMods auth failures rather than an up-front validation error.  
**Exact fix:** Make key_field a ValidatingLineEdit (VALID_NOT_EMPTY) and incorporate its validity into the OK-button enable logic, or validate the key format before accept().

---

## `src/ui/addapikeydialog.ui`

### F100 — 🟡 Low · Security · ✓verified
**Location:** `src/ui/addapikeydialog.ui:25`  
**Title:** NexusMods API key entered/displayed in plaintext (no password masking)  
**Problem:** The key_field in AddApiKeyDialog is a plain QLineEdit with default (Normal) echo mode, so the NexusMods API key, a long-lived bearer secret, is shown in cleartext as it is typed and remains visible on screen. By contrast the master password uses the masked PasswordField widget. This is inconsistent and exposes the secret to shoulder-surfing/screenshots even though the codebase clearly treats the API key as sensitive (it is AES-GCM encrypted at rest).  
**Exact fix:** Use a PasswordField (or set key_field echo mode to QLineEdit::Password with a show/hide toggle) for API key entry, matching the password fields.

---

## `src/ui/addappdialog.cpp`

### F010 — 🔴 High · Functionality · ✓verified
**Location:** `src/ui/addappdialog.cpp:342-345`  
**Title:** update_ignore_list JSON value wrongly assigned to separate_profile_dirs (copy-paste bug)  
**Problem:** In initConfigForApp() the value read from JSON_DEPLOYERS_UPDATE_IGNORE_LIST is assigned to info.separate_profile_dirs, the same field already set from JSON_DEPLOYERS_SEPARATE_DIRS one line above. So the deployer's update_ignore_list flag is never populated, and the uses_separate_dirs setting is silently overwritten by whatever update_ignore_list contains. ReverseDeployers imported from steam_app_configs get the wrong separate-dirs / ignore-list behaviour. The identical bug exists in initConfigForGog() at lines 914-917.  
**Exact fix:** Assign the update_ignore_list value to info.update_ignore_list (not info.separate_profile_dirs) in both initConfigForApp() and initConfigForGog().

### F022 — 🟠 Medium · Security · ✓verified
**Location:** `src/ui/addappdialog.cpp:283-301`  
**Title:** Deployer target_dir from community game config is created on disk without path validation  
**Problem:** initConfigForApp() reads target_dir from a JSON game definition that, per fork #204, can be dropped into a user-writable AppConfigLocation/game_configs dir (and is searched before the bundled one). After $STEAM_INSTALL_PATH$/$STEAM_PREFIX_PATH$ placeholder substitution the resolved path is passed straight to sfs::create_directories with no canonicalization or containment check, so a malicious/sloppy definition (e.g. target '/home/user/.config/autostart' or absolute paths outside any prefix) silently materialises arbitrary directory trees on accept. initConfigForGog() at line 853 reuses the same pattern but only skips non-existent targets (less dangerous).  
**Exact fix:** Validate resolved target/source paths before create_directories: reject absolute paths that escape the expected install/prefix roots, reject '..' components, and restrict directory creation to inside the staging/prefix/install dirs. Surface skipped deployers to the user.

### F063 — 🟠 Medium · Functionality
**Location:** `src/ui/addappdialog.cpp:477-520`  
**Title:** Deploy hooks silently discarded when the app config file does not yet exist  
**Problem:** saveHooksToConfig() returns early (no-op) when the lmm_mods.json config does not exist at the staging dir, and on_buttonBox_accepted() calls it only in edit mode. The header comment admits hooks 'can be set later'. A user who edits an app and fills in pre/post deploy hooks before the config exists, or whose config write fails (lines 494, 504, 516 only Log::debug), gets no error feedback and their hook commands are silently dropped. There is no UI indication that the hooks were not saved.  
**Exact fix:** Surface a warning to the user when hooks could not be persisted, or route hooks through EditApplicationInfo/setDeployHooks so they are saved atomically with the rest of the edit rather than via a best-effort side-write.

### F174 — 🟡 Low · Functionality
**Location:** `src/ui/addappdialog.cpp:152-158`  
**Title:** iconIsValid loads the full icon via QIcon just to validate it  
**Problem:** iconIsValid() constructs a QIcon from a user-supplied path and calls availableSizes() to decide validity. For a path pointing at a very large image this does real decoding work on the UI thread on every validation, and any path the user types into icon_field_ (no extension/type restriction) is handed to QIcon. Combined with no file-type allowlist this is a minor performance/robustness concern.  
**Exact fix:** Cheaply pre-check the path is an existing regular file with an image extension before constructing QIcon, and consider validating off the UI thread for large files.

### F175 — 🟡 Low · Functionality
**Location:** `src/ui/addappdialog.cpp:660`  
**Title:** Imported Steam app id parsed with toLong() without validation  
**Problem:** onApplicationImported() sets steam_app_id_ = app_id.toLong() with no ok-flag check. If the import dialog ever supplies a non-numeric or oversized app id, steam_app_id_ becomes 0 and the launch command 'xdg-open steam://rungameid/' + app_id is built from the unvalidated string, producing a broken launch command with no warning.  
**Exact fix:** Parse with the bool* ok flag and validate before assigning; warn or refuse if the app id is not a valid positive integer.

### F176 — 🟡 Low · Functionality
**Location:** `src/ui/addappdialog.cpp:677-689,993-1006`  
**Title:** QFileDialog instances are heap-allocated with no parent and never deleted  
**Problem:** on_icon_picker_button_clicked() and on_gog_prefix_picker_button_clicked() do 'auto dialog = new QFileDialog;' with no parent and no deleteLater, then call exec(). Each open of these pickers leaks a QFileDialog. The same pattern appears in adddeployerdialog.cpp (on_file_picker_button_clicked, on_source_picker_button_clicked) and edittoolwidget.cpp runFileDialog(). Minor but unbounded over a long session.  
**Exact fix:** Pass 'this' as parent and/or use a stack-allocated QFileDialog, or call dialog->deleteLater() after exec().

---

## `src/ui/addmoddialog.cpp`

### F242 — 🟡 Low · Code Quality
**Location:** `src/ui/addmoddialog.cpp:401`  
**Title:** Bitwise OR used where logical OR is intended for boolean deployer-target check  
**Problem:** is_target is computed as 'selected_deployers.contains(i) | (i == cur_deployer)' using bitwise OR on two bools. It works by accident here, but bitwise OR defeats short-circuiting and is misleading; a future change to either side returning a non-0/1 int would break it.  
**Exact fix:** Use logical || : 'selected_deployers.contains(i) || (i == cur_deployer)'.

---

## `src/ui/addprofiledialog.cpp`

### F243 — 🟡 Low · Code Quality
**Location:** `src/ui/addprofiledialog.cpp:16-46`  
**Title:** Uninitialized int members app_id_ and profile_  
**Problem:** AddProfileDialog declares app_id_ and profile_ as plain ints with no in-class initializer (addprofiledialog.h:39,41) and the constructor does not set them. They are only assigned in setAddMode/setEditMode, so emitting profileAdded/profileEdited before either is called would read indeterminate values. Same pattern (uninitialised app_id_/steam_app_id_/deployer_id_/tool_id_) exists across the other dialogs.  
**Exact fix:** Give the members in-class default initializers (e.g. 'int app_id_ = -1;') so a mis-sequenced call fails predictably.

---

## `src/ui/addtodeployerdialog.cpp`

### F177 — 🟡 Low · Functionality
**Location:** `src/ui/addtodeployerdialog.cpp:28-38`  
**Title:** Parallel access to auto_deployers[i] indexed by deployer_names size with no size check  
**Problem:** setupDialog iterates i over deployer_names.size() and dereferences auto_deployers[i] without verifying auto_deployers has at least that many elements. If a caller ever passes mismatched-length vectors (deployer_names longer than auto_deployers), this is an out-of-bounds read. Current callers appear to keep them in sync, but the contract is undocumented and unchecked.  
**Exact fix:** Guard with `i < auto_deployers.size()` (and similarly for mod_deployers logic), or assert the vectors are the same length at entry.

---

## `src/ui/applicationmanager.cpp`

### F064 — 🟠 Medium · Functionality
**Location:** `src/ui/applicationmanager.cpp:23-26`  
**Title:** Download filename regex requires a query string and rejects valid URLs  
**Problem:** performDownload extracts the filename with regex R"(.*/(.*)\?.*)" which only matches URLs containing a '?'. A redirected or signed CDN download URL without a query string (common) fails std::regex_match and throws 'Invalid download URL', aborting an otherwise valid download. The result depends on the exact form of the NexusMods/CDN response, so this is an edge case that breaks downloads in normal use for certain providers.  
**Exact fix:** Use a regex that makes the query optional, e.g. R"(.*/([^/?]+)(?:\?.*)?$)", or parse with QUrl (QUrl::fileName()) instead of a hand-rolled regex.

### F065 — 🟠 Medium · Functionality
**Location:** `src/ui/applicationmanager.cpp:412-419`  
**Title:** deployerIndexIsValid dereferences apps_[app_id] without validating app_id  
**Problem:** deployerIndexIsValid immediately calls apps_[app_id].getNumDeployers() with no bounds check on app_id. It relies entirely on every caller having already called appIndexIsValid(app_id) on the same line. This is a fragile invariant: any future call site (or a refactor that reorders the short-circuit &&) that invokes it with an out-of-range or negative app_id triggers out-of-bounds vector access (UB/crash). The companion appIndexIsValid does bounds-check, making this asymmetry an easy trap.  
**Exact fix:** Add an explicit bounds check at the top of deployerIndexIsValid: if(app_id < 0 || app_id >= (int)apps_.size()) return false; before indexing apps_[app_id].

### F066 — 🟠 Medium · Functionality
**Location:** `src/ui/applicationmanager.cpp:1454-1480`  
**Title:** Download retry/queue item discards version_overwrite and target_group_id on completion  
**Problem:** downloadMod constructs the queue item with `version_overwrite` and `target_group_id` (1418-1419) and importInfoForItem restores them on retry, but the per-item JSON in saveDownloadQueueLocked persists them while the emitted `downloadComplete(info)` carries the locally-passed `info`. For a retried download, info is rebuilt by importInfoForItem so these survive; however the initial queued item created from a fresh ImportModInfo only copies these if the caller set them. There is no validation that target_group_id refers to a still-existing group by the time the queued download completes after a restart, so a stale group id from lmm_queue.json flows into installMod's group logic (moddedapplication.cpp:195-204) unchecked.  
**Exact fix:** Validate target_group_id against the app's current groups when consuming a restored queue item, and clear it (set -1) if the group no longer exists before emitting downloadComplete.

### F067 — 🟠 Medium · Functionality
**Location:** `src/ui/applicationmanager.cpp:1531-1541`  
**Title:** Download queue file path changes after the first app is added, orphaning a previously saved queue  
**Problem:** getDownloadQueuePath() returns `apps_.front().getDownloadDir()/lmm_queue.json` when any app exists, but falls back to the user AppDataLocation when `apps_` is empty. loadDownloadQueue() runs in init() after updateState() populates apps_, so the location depends on whether app 0 exists at load time. If the queue was saved while no apps existed (AppData path) and an app is later added, subsequent saves/loads target the app-0 download dir, silently losing the previously persisted queue. The chosen path also moves if app 0 is removed/reordered, since front() is positional.  
**Exact fix:** Use a single stable location for the queue file (e.g. always the user AppDataLocation) independent of which apps exist or their ordering, so the persisted queue is found consistently across restarts.

### F101 — 🟡 Low · Security · ✓verified
**Location:** `src/ui/applicationmanager.cpp:22`  
**Title:** Full remote download URL (with signed token/expiry) is written to the debug log  
**Problem:** performDownload logs the entire `info.remote_download_url` at LOG_DEBUG. NexusMods/CDN download URLs embed time-limited signed query parameters (key/expires); for nxm:// flows the token-bearing URL is logged verbatim. While debug-level and time-limited, this writes a credential-equivalent URL into log output/files that may be shared in bug reports.  
**Exact fix:** Log only the URL path/basename (strip the query string) at debug level, or redact the signed parameters before logging.

### F244 — 🟡 Low · Code Quality
**Location:** `src/ui/applicationmanager.cpp:84-100`  
**Title:** Download size formatting uses signed long and signed/unsigned comparison  
**Problem:** The human-readable size string builder assigns 'long size = download_total' (download_total is the cpr total which can exceed LONG_MAX on 32-bit and is conceptually unsigned), and the loop condition 'exp < units.size()' compares a signed int against size_t (unsigned), producing a signed/unsigned comparison warning. The 'last_size /= 1.024' arithmetic mixing long and double, plus digit extraction, is convoluted and only affects a log string, but the signed truncation could misformat very large sizes.  
**Exact fix:** Use unsigned/long long for size accumulation, cast units.size() to int (or use size_t exp), and simplify the unit formatting; this is display-only so correctness over edge sizes matters less than the signed-overflow cleanliness.

### F178 — 🟡 Low · Functionality
**Location:** `src/ui/applicationmanager.cpp:215-232`  
**Title:** auto_update_check_in_progress_ guard not exception-safe  
**Problem:** runScheduledUpdateCheck sets auto_update_check_in_progress_ = true (line 224), calls checkForModUpdates synchronously, then sets it back to false (line 231). checkForModUpdates routes through handleExceptions which can re-throw when throw_exceptions_ is enabled. If an exception propagates out, the in-progress flag is never reset, permanently disabling all future scheduled checks for the session. Even without throw_exceptions_, the flag is redundant since the synchronous worker-thread design already serializes checks.  
**Exact fix:** Reset the flag in an RAII guard / try-finally (or a scope-exit) so it is cleared even if checkForModUpdates throws.

### F179 — 🟡 Low · Functionality
**Location:** `src/ui/applicationmanager.cpp:253-261`  
**Title:** Auto-update-check flag/interval read without synchronization across threads  
**Problem:** autoUpdateCheckEnabled()/autoUpdateCheckIntervalHours() are const getters that read auto_update_check_enabled_ and auto_update_check_interval_hours_ directly. These members are written on the worker thread inside the queued lambda in setAutoUpdateCheck (line 243-244) and in initAutoUpdateCheck, while the getters are intended for the settings dialog on the UI thread. This is a data race on plain bool/int (benign on most platforms but undefined behavior per the C++ memory model); the comments elsewhere stress marshalling onto the owning thread, so the getters are an unguarded read of the same state.  
**Exact fix:** Make the getters marshal/read via the owning thread, use std::atomic for the flag/interval, or document that the values are only eventually-consistent snapshots.

### F180 — 🟡 Low · Functionality
**Location:** `src/ui/applicationmanager.cpp:544-558`  
**Title:** deployMods emits 'Mods deployed' success even when deployment was skipped or errored  
**Problem:** deployMods always emits completedOperations("Mods deployed") at line 557 regardless of outcome: if appIndexIsValid is false, if verifyDeployerDirectories returns an error code (handleAddDeployerError fires but code!=0 means deployMods is skipped), or if handleExceptions caught an exception. The user sees a 'Mods deployed' confirmation even when nothing was deployed or an error dialog was just shown. The same pattern repeats in deployModsFor (581) and forceRedeployMods (607).  
**Exact fix:** Only emit the success message on the successful path; emit a neutral completedOperations() (or no message) when validation fails or an error was already surfaced.

### F208 — 🟡 Low · UI
**Location:** `src/ui/applicationmanager.cpp:918-926`  
**Title:** Read-only getters emit a user-facing error dialog on invalid app_id  
**Problem:** getModColor (and getModColors at 928, getModCategory at 945, getTw3VanillaScriptsRoot, getRestorePoints) call appIndexIsValid(app_id) with the default show_error=true, so a pure read/getter invoked with a stale app_id pops a 'App index out of range' error dialog to the user instead of silently returning an empty value. Other getters in this file (getModInfo, getAppInfo, getProfileNames) correctly pass show_error=false. This inconsistency produces spurious error popups during normal UI teardown/app-switch races.  
**Exact fix:** Pass show_error=false in these read-only getters: appIndexIsValid(app_id, false), matching getModInfo/getAppInfo.

### F181 — 🟡 Low · Functionality
**Location:** `src/ui/applicationmanager.cpp:1194-1198`  
**Title:** extractArchive emits extractionComplete even when extraction failed  
**Problem:** extractArchive runs performExtraction through handleExceptionsForFunction (which swallows exceptions and emits sendError) and then unconditionally emits extractionComplete(info). On failure, performExtraction throws before setting last_action_was_successful = true, so info.last_action_was_successful stays false; downstream slots must check that flag. If any connected slot keys off the extractionComplete signal alone (rather than the flag), it proceeds on a failed extraction.  
**Exact fix:** Only emit extractionComplete when extraction succeeded, or document/guarantee that all consumers check info.last_action_was_successful.

### F182 — 🟡 Low · Functionality
**Location:** `src/ui/applicationmanager.cpp:1511-1523`  
**Title:** Successful download path does not reset cancel_active_download_  
**Problem:** The failure lambda resets `cancel_active_download_ = false` (line 1442), but the success block (1511-1523) only clears `active_download_id_`. If a user requests cancel just as the transfer completes successfully, the stale `true` persists; downloadCancelRequested() then returns true until the next download starts and re-clears it. Harmless in practice but inconsistent and could confuse future callers of downloadCancelRequested().  
**Exact fix:** Set `cancel_active_download_ = false;` in the success block alongside `active_download_id_ = -1;`.

### F183 — 🟡 Low · Functionality
**Location:** `src/ui/applicationmanager.cpp:1573-1574`  
**Title:** Download queue JSON write is not flushed/checked before being relied upon  
**Problem:** saveDownloadQueueLocked writes `file << json;` to an ofstream and lets it close at scope end without checking the stream state. A failed/partial write (full disk, etc.) is not detected; only constructor-thrown exceptions are caught. A truncated lmm_queue.json then fails to parse on next load (caught and logged), silently dropping the entire persisted queue.  
**Exact fix:** Check file.good() after writing and log a warning on failure; consider writing to a temp file and atomically renaming to avoid leaving a truncated queue file.

### F184 — 🟡 Low · Functionality
**Location:** `src/ui/applicationmanager.cpp:1677-1685`  
**Title:** cancelDownload on the active item does not persist or emit the queue change  
**Problem:** When cancelling the currently active download, cancelDownload sets `cancel_active_download_ = true` and returns immediately without calling saveDownloadQueueLocked()/emitDownloadQueueLocked(). The UI only reflects the cancellation once the next progress callback fires (reportDownloadProgress emits) or the transfer aborts. For a stalled/slow transfer with no progress callbacks, the user gets no immediate feedback that cancel was registered.  
**Exact fix:** After setting the cancel flag, emit a queue snapshot (or a transient state) so the UI can show "Cancelling..." immediately rather than waiting for the next cpr progress tick.

---

## `src/ui/assetpreviewdialog.cpp`

### F102 — 🟡 Low · Security · ✓verified
**Location:** `src/ui/assetpreviewdialog.cpp:210-231`  
**Title:** Asset preview scan follows symlinks out of the staging directory  
**Problem:** scanForFiles uses QDirIterator with QDirIterator::Subdirectories and no flag to skip symlinks; QFileInfo::absoluteFilePath does not resolve/clamp to the staging root. A malicious mod archive that extracts a symlink (e.g. link.ini -> /etc/passwd or ../../other_mod/secret) inside its staging dir would have the link's target listed and its contents shown in the preview pane. The MAX_DEPTH check uses the relative path which a symlink can keep shallow. Read-only here, but it discloses arbitrary readable files outside the mod.  
**Exact fix:** Skip symlinks during the scan (e.g. check QFileInfo::isSymLink()) or canonicalize each result and verify it is still under staging_path_ before adding it to file_paths_.

### F185 — 🟡 Low · Functionality
**Location:** `src/ui/assetpreviewdialog.cpp:285`  
**Title:** Text preview assumes UTF-8 and silently mangles other encodings  
**Problem:** previewText reads up to MAX_TEXT_SIZE bytes and passes them straight to QString::fromUtf8. Many game config/INI files (e.g. legacy Bethesda/Windows-authored .ini) are Latin-1 or UTF-16; invalid UTF-8 sequences are replaced with U+FFFD, so the preview shows corrupted text with no indication that decoding failed. Reading exactly MAX_TEXT_SIZE can also split a multibyte sequence at the boundary.  
**Exact fix:** Use QStringDecoder with BOM/encoding detection (or at least fall back to Latin-1 when the bytes are not valid UTF-8) and surface the detected encoding in the header.

---

## `src/ui/backuplistmodel.cpp`

### F186 — 🟡 Low · Functionality
**Location:** `src/ui/backuplistmodel.cpp:58, 86`  
**Title:** backup name lookup indexes backup_names by cur_active_member without bounds/empty check  
**Problem:** data() returns targets_[row].backup_names[targets_[row].cur_active_member] for backup_col and backup_name_role. If backup_names is empty or cur_active_member is out of range (e.g. a target with zero backups, or an index left stale after a backup deletion), this is an out-of-bounds std::vector access from within the const data() override, crashing during paint.  
**Exact fix:** Bounds-check cur_active_member against backup_names.size() and return an empty string when out of range or the list is empty.

---

## `src/ui/bsabrowserdialog.cpp`

### F006 — 🔴 High · Security · ✓verified
**Location:** `src/ui/bsabrowserdialog.cpp:159-162`  
**Title:** Path traversal when extracting BSA/BA2 entries (internal path joined to dest dir unsanitized)  
**Problem:** The internal entry path comes straight from the archive's name table, which is fully attacker-controlled (BsaArchive::parseBsa builds entry.path from the file-name block, parseBa2 from the name table, both via normalizePath which only converts backslashes to forward slashes). In onExtractSelected the destination is built as `sfs::path(dest_dir.toStdString()) / sfs::path(internal_std)` with no validation. std::filesystem::operator/ replaces the left operand entirely when the right operand is absolute (e.g. internal path "/etc/cron.d/x" or "/home/user/.bashrc"), and traversal components like "../../../.config/autostart/x.desktop" walk out of the chosen directory. BsaArchive::writeEntry then calls create_directories(dest.parent_path()) and writes the entry there. A malicious .bsa/.ba2 can therefore write arbitrary files outside the user-selected extraction directory.  
**Exact fix:** Before extracting, reject or sanitize entry paths: treat the internal path as relative, strip any leading '/', reject paths whose lexically_normal form contains a leading '..' component, and verify weakly_canonical(dest) starts with weakly_canonical(dest_dir). Skip/raise on any entry that would resolve outside the destination directory.

---

## `src/ui/changeapipwdialog.cpp`

### F245 — 🟡 Low · Code Quality
**Location:** `src/ui/changeapipwdialog.cpp:14`  
**Title:** Constructor member-initializer list order does not match declaration order  
**Problem:** The init list orders uses_default_pw_/cipher_/nonce_/tag_ before the QDialog base and ui, but members are always initialized in declaration order (ui, cipher_, nonce_, tag_, dialog_completed_, uses_default_pw_) and the base before all members. This triggers -Wreorder and is misleading; line 29 then redundantly re-assigns uses_default_pw_ = uses_default_pw. No runtime bug today since there are no inter-member dependencies, but it is fragile.  
**Exact fix:** Reorder the initializer list to match declaration order (base class first), and drop the redundant assignment on line 29.

---

## `src/ui/changelogdialog.cpp`

### F187 — 🟡 Low · Functionality
**Location:** `src/ui/changelogdialog.cpp:36-40`  
**Title:** Changelog JSON parsing has no error handling for malformed file  
**Problem:** `file >> json;` (line 37) is not wrapped in try/catch; a corrupt or partially-written changelogs.json throws a Json::Exception that propagates out of the dialog constructor. Unlike mainwindow.cpp's config loader which guards JSON parsing, here a malformed bundled file aborts dialog construction. The file is install-bundled (low likelihood) but the failure mode is an uncaught exception rather than a graceful empty changelog.  
**Exact fix:** Wrap the JSON read in try/catch, log the error, and leave versions_ empty so the dialog opens with no entries instead of throwing.

---

## `src/ui/deployedfilestreedialog.cpp`

### F246 — 🟡 Low · Code Quality
**Location:** `src/ui/deployedfilestreedialog.cpp:30-82`  
**Title:** Deployed-files tree built O(n*depth) with no progress/empty state for large deployments  
**Problem:** The constructor builds the entire tree synchronously, calling expandAll and resizeColumnToContents at the end. For deployments with tens of thousands of files this blocks the UI thread with no progress indicator, and there is no explicit empty-state message when origins is empty (the tree simply shows nothing beyond the summary). Given deployment file counts can be large, this is a noticeable UX/perf gap.  
**Exact fix:** Defer expandAll/resize or cap auto-expansion for large trees, and show an explicit 'no deployed files' placeholder when origins is empty.

---

## `src/ui/deployerlistmodel.cpp`

### F068 — 🟠 Medium · Functionality
**Location:** `src/ui/deployerlistmodel.cpp:64-65, 74-79`  
**Title:** data() reinterprets every node as DeployerModInfo, including separator/root DeployerEntry objects  
**Problem:** Line 65 unconditionally builds a shared_ptr<DeployerModInfo> aliasing the node's raw data pointer. Separators and the root are constructed as plain DeployerEntry (addSeparator() line 255; deployer.cpp/moddedapplication.cpp), which has none of DeployerModInfo's members. The Qt::ForegroundRole branch (74-79) reads modinfo->id for ALL items; id happens to live in the DeployerEntry base so it is in-bounds, but this pattern is fragile: any future role that reads a DeployerModInfo-only member (sourceName/enabled/tags) without first checking data->isSeparator would read past the smaller DeployerEntry object (undefined behavior). collectTags already gates this with !isSeparator; data() should too.  
**Exact fix:** Only form the DeployerModInfo alias after confirming !data->isSeparator (dynamic_cast or the isSeparator flag), and never read derived members for separator/root nodes. Consider storing a discriminated type rather than relying on reinterpret/static aliasing.

### F069 — 🟠 Medium · Functionality
**Location:** `src/ui/deployerlistmodel.cpp:94, 137`  
**Title:** Indexing source_mod_names_/valid_mod_actions by tree row can read out of bounds  
**Problem:** In data(), col == id_col uses deployer_info_.source_mod_names_[row] (line 94) and valid_mod_actions_role uses deployer_info_.valid_mod_actions[row] (line 137), where row is the tree-node row within its parent. These vectors are sized per flat mod list, not per tree position; with separators present (rows are positions under a parent, and separators occupy rows too) the row index does not correspond to the vector index and can exceed the vector size, giving an out-of-bounds std::vector::operator[] read (UB/crash) on deployers that use source references or per-mod actions.  
**Exact fix:** Index these vectors by the mod's stable id/flat index rather than the tree row, and bounds-check before operator[] (or use .at() within a guarded try, or return empty when out of range).

---

## `src/ui/deployerlistproxymodel.cpp`

### F070 — 🟠 Medium · Functionality
**Location:** `src/ui/deployerlistproxymodel.cpp:16-28`  
**Title:** Proxy data() ForegroundRole indexes row_text_colors_ by proxy row that may be stale after filtering  
**Problem:** DeployerListProxyModel::data() returns row_text_colors_[index.row()] for Qt::ForegroundRole. row_text_colors_ is rebuilt in updateFilter() by iterating rowCount() proxy rows, but data() can be invoked by the view between a source-model layoutChanged/filter change and the next updateFilter() call, when row_text_colors_.size() no longer matches the current proxy row count. The guard only checks row >= size(); if the vector is longer/shorter-but-nonzero relative to the new mapping, a stale/incorrect colour is returned, and the indexing assumes proxy rows are dense and ordered identically to updateFilter's iteration.  
**Exact fix:** Key the colour by mod id (compute conflict colour on demand from conflict_groups_) instead of caching by proxy row, or invalidate/rebuild row_text_colors_ on every filter/layout change before the view repaints.

### F188 — 🟡 Low · Functionality
**Location:** `src/ui/deployerlistproxymodel.cpp:105-108`  
**Title:** Tag-filter boolean logic relies on operator precedence without parentheses  
**Problem:** In filterAcceptsRow, show *= contains_tag && enabled || !contains_tag && !enabled; relies on && binding tighter than ||. While correct by C++ precedence, the same unparenthesised pattern is duplicated in modlistproxymodel.cpp:122 and is easy to misread/break during edits; combined with using *= and |= on a bool as integer arithmetic for boolean logic, the filter intent is obscure.  
**Exact fix:** Parenthesise as ((contains_tag && enabled) || (!contains_tag && !enabled)) and use proper boolean operators (show = show && ...) instead of *= / |= arithmetic on bool.

---

## `src/ui/deployerlistview.cpp`

### F247 — 🟡 Low · Code Quality
**Location:** `src/ui/deployerlistview.cpp:24-30`  
**Title:** setModel creates a single-shot QTimer that is started but never connected to any slot  
**Problem:** In DeployerListView::setModel, on every layoutChanged a new QTimer is allocated (parented to this), set single-shot, and start()ed, but it is connected to nothing; expandSeparators is instead called synchronously right after. The timer does nothing except leak one QTimer object per layoutChanged emission for the lifetime of the view (they are never deleted until the view is destroyed), and the dead code obscures the intent (likely a deferred-expand that was never wired up).  
**Exact fix:** Remove the dead QTimer, or if deferral is intended, connect timer->timeout to a lambda calling expandSeparators and delete the timer in that slot (or use QTimer::singleShot).

### F248 — 🟡 Low · Code Quality
**Location:** `src/ui/deployerlistview.cpp:132, 144`  
**Title:** rowsAboutToBeRemoved called directly as a function instead of via beginRemoveRows/endRemoveRows  
**Problem:** During drag reorder, the view calls rowsAboutToBeRemoved(...) directly on itself. rowsAboutToBeRemoved is the model's protected signal-emitting slot, not a view API; calling it here does not properly bracket the structural change (no matching endRemoveRows/begin-end pair on the model), so the model/view can be left with an inconsistent internal index mapping after the move, risking stale QModelIndex/persistent index corruption.  
**Exact fix:** Perform structural moves through the model API using beginMoveRows/endMoveRows (or beginRemoveRows/endRemoveRows + begin/endInsertRows) inside the model, and have the view request the move rather than poking signal emitters directly.

---

## `src/ui/editautotagsdialog.cpp`

### F249 — 🟡 Low · Code Quality
**Location:** `src/ui/editautotagsdialog.cpp:248-254`  
**Title:** Signed/unsigned comparison in onConditionRemoved bounds check  
**Problem:** `if(row >= conditions.size())` compares a signed int `row` against an unsigned size_type. A negative row (not expected from TablePushButton but possible if signature changes) would be converted to a large unsigned value and pass the guard, then `conditions.begin() + row` would be UB. Minor robustness/quality issue.  
**Exact fix:** Use `if(row < 0 || static_cast<size_t>(row) >= conditions.size()) return;`.

---

## `src/ui/editmanualtagsdialog.cpp`

### F011 — 🔴 High · Functionality · ✓verified
**Location:** `src/ui/editmanualtagsdialog.cpp:143-153`  
**Title:** Duplicate-name rename is reported as an error but still applied, corrupting tag state  
**Problem:** onTableCellEdited detects a duplicate name, shows a 'Renaming Failed' QMessageBox and calls updateTable(), but there is no return. Execution falls through to emplace a rename action and assign tag_names_[row] = new_name. The result is that a rename the user was just told failed is actually recorded and emitted to the backend, and tag_names_ now contains a duplicate, violating the 'names must be unique' invariant documented in the header.  
**Exact fix:** Add `return;` after updateTable() inside the duplicate branch so the rejected rename is not recorded or applied.

### F250 — 🟡 Low · Code Quality
**Location:** `src/ui/editmanualtagsdialog.cpp:157-177`  
**Title:** Dead loop in on_buttonBox_accepted computes type_str and discards it  
**Problem:** After emitting manualTagsEdited, the function loops over actions_ building a local type_str string ('Add'/'Rename'/'Remove') that is never used. This is dead code, likely a leftover from removed logging/debugging, and it obscures intent.  
**Exact fix:** Remove the unused loop entirely.

---

## `src/ui/edittoolwidget.cpp`

### F071 — 🟠 Medium · Functionality
**Location:** `src/ui/edittoolwidget.cpp:185-231`  
**Title:** Steam tool app id parsed with toInt() silently yields 0 on overflow/empty  
**Problem:** app_id_field_ accepts an unbounded digit string (regex '[0-9]*') and getTool() converts it via app_id_field_->text().toInt() for both the Steam runtime (line 196) and Protontricks runtime (line 226). QString::toInt returns 0 on overflow or empty input with no error checking, so an app id larger than INT_MAX (or left blank in a code path where the field is enabled) becomes steam_app_id 0, producing '-applaunch 0' / '--appid 0'. The validator also permits an empty string, so a Steam/Protontricks tool can be created with no app id.  
**Exact fix:** Use toLongLong with the bool* ok flag, validate the result fits the steam_app_id_ type, and require a non-empty app id (set VALID_NOT_EMPTY / a min-length validator) when the Steam or Protontricks runtime is selected.

### F209 — 🟡 Low · UI
**Location:** `src/ui/edittoolwidget.cpp:34,42,70,94,124`  
**Title:** Icon/file picker buttons and command field lack accessible names  
**Problem:** icon_picker_, executable_picker_, prefix_picker_ and working_directory_picker_ are icon-only QPushButtons created with no text and no setAccessibleName/setToolTip, so screen readers announce them as unlabeled buttons and there is no hover hint. command_field_ also has no placeholder text or tooltip describing the expected shell command. This is an accessibility gap for keyboard/screen-reader users navigating the tool editor.  
**Exact fix:** Add setAccessibleName()/setToolTip() (e.g. 'Browse for executable', 'Browse for icon') to each picker button and a placeholder/tooltip on command_field_.

---

## `src/ui/externalchangesdialog.cpp`

### F189 — 🟡 Low · Functionality
**Location:** `src/ui/externalchangesdialog.cpp:47`  
**Title:** Unchecked .at(i) on file_changes indexed by list-widget row  
**Problem:** on_buttonBox_accepted iterates ui->file_list->count() and indexes changes_info_.file_changes.at(i). This relies on the list widget always having exactly as many rows as file_changes entries. setup() populates them 1:1, so today it holds, but if a future code path ever filters/reorders the list or adds non-change items, .at(i) throws std::out_of_range and crashes the accept handler. An indexing assumption tied to UI row count is fragile.  
**Exact fix:** Iterate over changes_info_.file_changes directly (bounded by its size) and read the matching list item via item(i) only when i < count(), or store the change index in the QListWidgetItem data role.

---

## `src/ui/fomodcheckbox.cpp`

### F251 — 🟡 Low · Code Quality
**Location:** `src/ui/fomodcheckbox.cpp:22`  
**Title:** FOMOD plugin image re-decoded from disk on every hover with no caching  
**Problem:** FomodCheckBox::enterEvent (and the identical FomodRadioButton::enterEvent) constructs a QPixmap from image_path_ every time the mouse enters the button. Hovering across a long plugin list repeatedly re-reads and re-decodes the same image files from the staging directory, which is wasteful for large textures and noticeably duplicated logic between the two classes.  
**Exact fix:** Decode the QPixmap once (e.g. lazily on first hover and cache it as a member), and factor the shared hover/preview logic out of the two near-identical button classes.

---

## `src/ui/fomoddialog.cpp`

### F190 — 🟡 Low · Functionality
**Location:** `src/ui/fomoddialog.cpp:243-246`  
**Title:** Operator-precedence ambiguity in selectionIsValid validation expression  
**Problem:** The combined condition mixes && and || without parentheses: 'type==at_least_one && num_selected==0 || type==at_most_one && num_selected>1 || ...'. It happens to be correct because && binds tighter than ||, but the intent is non-obvious and a future edit can easily break it. This is the gate that decides whether a FOMOD step's selection is acceptable before advancing.  
**Exact fix:** Parenthesize each (type==X && cond) group explicitly to make the precedence intent unambiguous and edit-safe.

### F103 — 🟡 Low · Security · ✓verified
**Location:** `src/ui/fomoddialog.cpp:385-391`  
**Title:** FOMOD choices JSON keys/values read from sidecar with no type validation  
**Problem:** loadChoices iterates root.getMemberNames() and root[key] assuming every top-level value is a JSON array of strings. If the .fomod_choices.json file (which lives inside a user-writable mod directory) contains a key whose value is a scalar/object, iterating 'for(const auto& name : root[key])' yields no/odd members and name.asString() on a non-string node can throw Json::LogicError. It is caught by the surrounding try/catch so it degrades to 'silently ignore', but a partially-corrupt file silently discards all saved choices instead of the valid subset.  
**Exact fix:** Validate root[key].isArray() and name.isString() before inserting; skip malformed entries individually so one bad key does not discard the whole file.

---

## `src/ui/importfromsteamdialog.cpp`

### F191 — 🟡 Low · Functionality
**Location:** `src/ui/importfromsteamdialog.cpp:279-290`  
**Title:** on_buttonBox_accepted dereferences table item pointers without null checks  
**Problem:** After selecting a row the code calls ui->app_table->item(row, 0)->text(), item(row,1), item(row,2), item(row,3) with no null guard. addTableRow always populates all four columns so this is normally safe, but any future code path that inserts a row with a missing cell (or a sort/filter race) would dereference nullptr and crash. The accompanying currentRow bounds check does not protect against a per-cell null item.  
**Exact fix:** Guard each item() result against nullptr before calling text()/data(), and bail out gracefully if any required cell is missing.

---

## `src/ui/ipcserver.cpp`

### F072 — 🟠 Medium · Functionality
**Location:** `src/ui/ipcserver.cpp:21`  
**Title:** IpcServer::setup() return value ignored; stale socket silently breaks nxm:// handling  
**Problem:** MainWindow::setupIpcServer (mainwindow.cpp:1897) calls ipc_server_->setup() and discards its bool. QLocalServer::listen() in setup() will fail if a stale socket file ('_Limo_Server_') is left behind by a crashed instance, and there is no QLocalServer::removeServer() call to clear it. The failure is silent, so the running instance never receives nxm:// download links forwarded from a second invocation, and the NexusMods download-via-browser flow stops working until the user manually deletes the socket.  
**Exact fix:** Check the return of setup(); on failure call QLocalServer::removeServer(server_name) and retry listen(). Log an error if it still fails so the broken IPC state is visible.

### F192 — 🟡 Low · Functionality
**Location:** `src/ui/ipcserver.cpp:38`  
**Title:** processData has no message framing; partial reads silently drop requests  
**Problem:** processData() reads whatever bytes are currently available on readyRead and emits them as one message. QLocalSocket can deliver a single write in multiple readyRead signals (or coalesce multiple writes), so a long nxm:// URL could be split and each fragment fails nxmUrlIsValid in onReceiveIpcMessage and is dropped. In practice URLs are short and usually arrive in one chunk, but there is no length prefix or delimiter to guarantee correct reassembly.  
**Exact fix:** Buffer received bytes per-socket and use a length prefix or newline delimiter to reassemble complete messages before emitting receivedMessage.

---

## `src/ui/loadorderbisectdialog.cpp`

### F210 — 🟡 Low · UI
**Location:** `src/ui/loadorderbisectdialog.cpp:34-46`  
**Title:** Bisect dialog has no empty/disabled state until a bisect step is reported  
**Problem:** When setEntries is called with an empty vector, restart()->nextStep() sees candidates_.size()<=1 and calls reportResult(), which shows the 'No load order entries' message. That path works, but updateDisplay (the normal interactive screen) never disables restart or clarifies state for a single-entry list (size==1 immediately reports a culprit without any test), which can confuse users into thinking a one-mod load order was 'bisected'. Minor UX gap.  
**Exact fix:** Special-case the 0- and 1-entry inputs with an explicit message that bisection is not applicable rather than presenting a 'culprit found' result for a single untested entry.

---

## `src/ui/mainwindow.cpp`

### F007 — 🔴 High · Security · ✓verified
**Location:** `src/ui/mainwindow.cpp:1613-1633 (runCommand), 1635-1655 (runConcurrent), callers 3285/4946/5339`  
**Title:** runCommand passes unescaped command strings straight to popen()/shell  
**Problem:** runCommand() builds a string and hands it to popen(command.toStdString().c_str(), "r"), which runs it through /bin/sh. The command originates from app launch command (info_command_label), tool commands (tools_[row].getCommand) and REDmod commands - all user/mod/import-supplied and never shell-escaped. The flatpak branch just prepends "flatpak-spawn --host " by string concatenation and " 2>&1" is appended, so any shell metacharacters (;, |, $(), backticks) in a tool/app command or in a path embedded into it execute arbitrarily. Because tool/app definitions can be set by imported MO2/Wabbajack/instance bundles, an imported config that sets an executable command becomes arbitrary command execution. This is the in-scope sink named in the task (mainwindow.cpp:1613).  
**Exact fix:** Avoid the shell entirely: use QProcess with an argv list (program + arguments) instead of popen on a shell string. If a shell is unavoidable, run via execvp-style argv or rigorously quote each argument; single-quote-only wrapping is insufficient because embedded single quotes break out. Treat imported tool/app command strings as untrusted and validate/escape them.

### F073 — 🟠 Medium · Functionality
**Location:** `src/ui/mainwindow.cpp:173-182`  
**Title:** Worker thread forcibly terminated on exit can corrupt mod state  
**Problem:** The destructor calls worker_thread_->quit(); wait(5000); and then, if the thread is still running, worker_thread_->terminate(). The ApplicationManager (moved onto worker_thread_) performs long filesystem operations: extracting archives (extractArchive), deploying/undeploying mods (deployMods/unDeployMods), pruning archives, and writing the per-app settings JSON. A deploy or extraction of a large mod list can easily exceed the 5s wait, after which terminate() kills the thread mid-operation. QThread::terminate() stops the thread at an arbitrary point with no stack unwinding, which can leave half-written deployed files, a partially written settings/config file (corrupted mod database), or stranded hard/sym links. Immediately afterward `delete app_manager_;` (line 181) destroys an object that may still have been executing, which is undefined behavior. There is also no thread-safety: app_manager_ lives on the worker thread but is deleted from the GUI thread without ensuring the worker has actually stopped touching it.  
**Exact fix:** Before destroying, signal the worker to stop accepting new work, disable the busy/long operations, and wait without a hard timeout (or a much larger one) for in-flight filesystem writes to finish. Avoid terminate() entirely for a worker that writes to disk; instead make long operations cancellable and join cleanly. Delete app_manager_ via deleteLater() on the worker thread (or only after wait() confirms the thread finished), never with a raw delete from the GUI thread after terminate().

### F074 — 🟠 Medium · Functionality
**Location:** `src/ui/mainwindow.cpp:1534-1542`  
**Title:** getColumnIndex dereferences a possibly-null header item  
**Problem:** getColumnIndex() calls table->horizontalHeaderItem(i)->text() inside the loop without checking for null. QTableWidget::horizontalHeaderItem returns nullptr for columns that have no explicit header item set, which would crash. It is called repeatedly from showEditDeployerDialog (lines 1414/1421/1422) on ui->info_deployer_list; if any of those columns lacks a header item the app crashes when editing a deployer.  
**Exact fix:** Guard the dereference: QTableWidgetItem* item = table->horizontalHeaderItem(i); if(item && item->text() == col_name) return i;

### F075 — 🟠 Medium · Functionality
**Location:** `src/ui/mainwindow.cpp:1631`  
**Title:** runCommand() return code computed as pclose()/256, mishandling signals and errors  
**Problem:** int ret_code = pclose(pipe) / 256; manually emulates WEXITSTATUS by integer-dividing the raw wait status. This ignores the case where pclose returns -1 (wait failed) and where the child was terminated by a signal (WIFSIGNALED), in which case the low byte holds the signal number and /256 yields 0 — a crashed/killed tool is reported as 'exited with return code 0' (success) in runConcurrent's log. Non-standard wait-status layouts also break this.  
**Exact fix:** Use the POSIX macros: capture int status = pclose(pipe); check for -1, then report WIFEXITED(status) ? WEXITSTATUS(status) : -(WTERMSIG(status)) (or a sentinel) so signal-terminated and failed-wait cases are distinguishable from a genuine 0 exit.

### F076 — 🟠 Medium · Functionality
**Location:** `src/ui/mainwindow.cpp:1998-2015`  
**Title:** versionIsLessOrEqual mishandles differing-length versions and non-numeric input  
**Problem:** versionIsLessOrEqual zips the two split version lists with stv::zip, which stops at the shorter list, so trailing components are ignored: e.g. "2.0.1" vs "2.0" compares only [2,0]==[2,0] and returns true (treated as <=), and "2.0" vs "2.0.0.1" also returns true while the extra precision is dropped. It also returns true whenever current_version contains any non-digit/non-dot char (regex_search at 2001), meaning a malformed stored app_version makes the function silently claim the version is old, triggering the LOOT URL migration in updateOutdatedSettings unexpectedly. Empty sub-components are skipped rather than treated as 0.  
**Exact fix:** Iterate over the max length, treating missing/empty components as 0, compare component-by-component returning on the first inequality, and decide an explicit policy for non-numeric input rather than defaulting to true.

### F077 — 🟠 Medium · Functionality
**Location:** `src/ui/mainwindow.cpp:3364-3367`  
**Title:** Null-pointer dereference in deployer tool menu actions when a column/cell is missing  
**Problem:** onVerifyDeployerMenuClicked, onDeployedFilesTreeMenuClicked, onHealthCheckDeployerMenuClicked and on_actionbrowse_deployer_files_triggered all do ui->info_deployer_list->item(deployer, getColumnIndex(ui->info_deployer_list, "Name"))->text(). getColumnIndex (lines 1534-1542) returns -1 when no header item matches, and QTableWidget::item(row, -1) returns nullptr, so ->text() dereferences null and crashes. The same getColumnIndex helper also unconditionally dereferences table->horizontalHeaderItem(i)->text() (line 1538), which is null if any column lacks a header item. The deployer index is bounds-checked against deployer_source_paths_, but nothing guarantees the info_deployer_list table has a populated cell at (deployer, columnIndex) or that the "Name"/"Mode" columns exist, so a refresh-timing mismatch or a renamed column header crashes the app instead of failing gracefully.  
**Exact fix:** Make getColumnIndex null-safe (skip null header items) and have every caller check both that getColumnIndex(...) >= 0 and that item(row, col) != nullptr before calling ->text(); bail out with a logged error otherwise.

### F252 — 🟡 Low · Code Quality
**Location:** `src/ui/mainwindow.cpp:1572 and 1596`  
**Title:** Operator-precedence-fragile log-visibility condition without parentheses  
**Problem:** Both log printers use: if(*show_error && level <= Log::LOG_ERROR || *show_warning && level <= Log::LOG_WARNING). Relying on && binding tighter than || is correct here but unparenthesised, making the intent non-obvious and easy to break on edit. This is duplicated verbatim in the main and tool log lambdas.  
**Exact fix:** Add explicit parentheses: (*show_error && level <= LOG_ERROR) || (*show_warning && level <= LOG_WARNING), and factor the two near-identical printer lambdas into one helper to remove the duplication.

### F193 — 🟡 Low · Functionality
**Location:** `src/ui/mainwindow.cpp:1635-1655`  
**Title:** runConcurrent logs full command and runs a member function touching shared state on the thread pool  
**Problem:** runConcurrent logs the entire command verbatim (line 1638) before running it; if a launch/tool command embeds credentials or tokens (common when launchers pass API keys/passwords as args), they are written to the on-disk log and the visible log frame. Additionally the QtConcurrent lambda captures this and calls runCommand, which reads the is_a_flatpak_ member from a pool thread; is_a_flatpak_ is set once at startup so this is currently safe, but the pattern of reaching back into the widget from a worker thread is fragile if more member state is added to runCommand.  
**Exact fix:** Consider not logging the raw command (or redacting obvious secret-looking args). Capture is_a_flatpak_ by value into the lambda rather than relying on the member access on the worker thread, to keep runCommand thread-safe as it evolves.

### F253 — 🟡 Low · Code Quality
**Location:** `src/ui/mainwindow.cpp:1942-1948`  
**Title:** checkForContainers calls getenv("container") twice  
**Problem:** checkForContainers() calls getenv("container") in the if-condition and again in the body to compare to "flatpak". This is redundant work and a minor TOCTOU smell; capture the value once. Not a correctness bug but avoidable duplication.  
**Exact fix:** const char* container = getenv("container"); is_a_flatpak_ = container && std::string(container) == "flatpak";

### F194 — 🟡 Low · Functionality
**Location:** `src/ui/mainwindow.cpp:2036-2090`  
**Title:** Root-level conditions parsed from steam_app_configs JSON with size() compared against signed int  
**Problem:** initRootLevelConditions() loops with for(int i = 0; i < json[JSON_ROOT_LEVEL_KEY].size(); i++) where Json::Value::size() returns an unsigned ArrayIndex, producing a signed/unsigned comparison. While these config files are app-shipped (lower trust than mod data), the parse only logs at debug level on failure, so a malformed shipped/overridden config silently yields no root-level conditions with no user-visible warning, which can cause mods to be installed at the wrong nesting level.  
**Exact fix:** Iterate with an unsigned/Json::ArrayIndex loop variable (or range-based over the array) and surface a visible warning (not just Log::debug) when a root-level config entry fails to parse so misconfiguration is noticeable.

### F195 — 🟡 Low · Functionality
**Location:** `src/ui/mainwindow.cpp:2101`  
**Title:** onModAdded uses QUrl::path() instead of toLocalFile() for dropped/added files  
**Problem:** info.local_source = url.path().toStdString(); takes the raw URL path rather than QUrl::toLocalFile(). For a local file URL, path() strips any query/fragment and does not apply the platform's local-file decoding rules, so a dropped archive whose absolute path legitimately contains '#' or '?' (e.g. /home/user/mod#2.zip) yields a truncated/incorrect path and the subsequent extraction fails or targets the wrong file. archiveUrlsFromMime already filters to url.isLocalFile() entries, so toLocalFile() is the correct accessor here. The same QUrl list is also delivered from internal list drag/drop (ModListView::modAdded), so any such filename breaks import silently.  
**Exact fix:** Use url.toLocalFile().toStdString() instead of url.path().toStdString() in onModAdded.

### F196 — 🟡 Low · Functionality
**Location:** `src/ui/mainwindow.cpp:2862-2902`  
**Title:** onExtractionComplete pops the import queue but does not continue the queue on dialog setup failure  
**Problem:** When add_mod_dialog_->setupDialog(...) returns false, the code shows an error via onReceiveError but does not call importMod() to continue processing remaining queued imports, nor does it re-enable drops/clear busy state consistently. The successful and last_action_was_successful==false branches handle queue continuation, but the setup-failure branch leaves any further queued mods stranded until another action triggers importMod.  
**Exact fix:** In the else (was_successful==false) branch, after showing the error, advance the queue (if(!mod_import_queue_.empty()) importMod();) and reset busy/drop state so a single failed import does not stall the rest of the batch.

### F254 — 🟡 Low · Code Quality
**Location:** `src/ui/mainwindow.cpp:3895-3903`  
**Title:** ETA computation can produce nonsensical remaining time on non-monotonic progress  
**Problem:** updateProgress computes remaining_sec = msecs_elapsed * (1.0 - progress) / progress. The guard progress - last_progress_ >= 0.01f prevents division by zero for the first sample, but progress is provided by the worker and is not validated to be monotonic or <= 1.0. If progress regresses or jumps, last_progress_update_time_ and last_progress_ get out of sync and the ETA can become negative or wildly large (it is only suppressed when remaining_sec <= 0). The int cast of a double that can overflow on a tiny progress value is also undefined for very large results.  
**Exact fix:** Clamp progress to [0,1], guard against progress <= 0 explicitly, compute the elapsed delta against the time of the previous accepted sample, and clamp the resulting ETA to a sane range before formatting.

### F197 — 🟡 Low · Functionality
**Location:** `src/ui/mainwindow.cpp:4989-5009`  
**Title:** IPC-triggered Nexus download uses currentApp() captured at message time with no validation  
**Problem:** onReceiveIpcMessage accepts an nxm:// URL from another process over the local IPC socket and queues a download targeting info.app_id = currentApp(). If no application is selected currentApp() can be -1 (or an unexpected index), and the queued download/import then proceeds against an invalid app id. The message is also acted on (activateWindow + queue) without confirming the URL host/game matches the current app, so an external process can silently queue downloads into whatever app happens to be selected. There is no rate limiting or user confirmation for externally injected download requests.  
**Exact fix:** Reject the request when currentApp() < 0 (show a status message), and consider confirming or at least logging the external origin before queueing. Validate that the queued ImportModInfo.app_id is a real application index before calling importMod().

### F213 — 🟡 Low · Feature Gap
**Location:** `src/ui/mainwindow.cpp:5206-5215`  
**Title:** Deploy preview (fork #49) confirm step is permanently dead code  
**Problem:** onExternalChangesHandled gates the DeployPreviewDialog on show_deploy_preview_ && !deploy_preview_plans_.empty(). Per the header (mainwindow.h:428-434) show_deploy_preview_ defaults to false and deploy_preview_plans_ is never populated anywhere in the codebase (no producer sets it), so the preview branch can never execute. The lengthy comment acknowledges the ApplicationManager signal that would fill it was never wired. The UI/action onShowDeploymentPreview exists and requests a separate preview, but the inline pre-deploy confirm advertised by this code path is non-functional, which is a feature gap / dead branch that can mask future wiring mistakes.  
**Exact fix:** Either complete the wiring (have ApplicationManager emit the computed plans and populate deploy_preview_plans_/show_deploy_preview_ from settings), or remove the dead branch so the deploy path is not misleading.

### F104 — 🟡 Low · Security · ✓verified
**Location:** `src/ui/mainwindow.cpp:5560-5598`  
**Title:** CSV formula injection in exported mod list  
**Problem:** on_actionExport_Mod_List_triggered writes mod names, tags and the Nexus remote_source into a CSV via the csv_field lambda, which only wraps fields in double quotes and doubles embedded quotes. It does not neutralise leading formula characters (=, +, -, @, tab/CR). Mod names, tags and remote_source are untrusted: they originate from mod archive contents and from NexusMods responses. A mod named =HYPERLINK("http://evil","click") or =cmd|'/c calc'!A1 will be interpreted as a formula when the exported CSV is opened in Excel/LibreOffice, enabling spreadsheet-side code execution / data exfiltration on whoever opens the export.  
**Exact fix:** In csv_field, if the (unquoted) value begins with =, +, -, @, 0x09 or 0x0d, prefix it with a single quote (') or a leading space before quoting, per OWASP CSV-injection guidance. Apply the same neutralisation to the Markdown export cells that embed untrusted text.

---

## `src/ui/managegroupsdialog.cpp`

### F085 — 🟠 Medium · UI
**Location:** `src/ui/managegroupsdialog.cpp:161-171`  
**Title:** Group rename accepts empty/whitespace names with no validation feedback  
**Problem:** on_rename_button_clicked stores ui->name_edit->text().trimmed() into pending_names_ unconditionally. An empty or whitespace-only name is staged and on commit emits groupRenamed(app_id_, group, ''). groupDisplayName then falls back to 'Group N', so the user's rename silently produces a blank/auto name with no error shown. There is also no duplicate-name check across groups, unlike the manual-tag editor.  
**Exact fix:** Reject empty/whitespace names (disable the rename button or show a warning), and optionally warn on duplicates, before staging the pending name.

---

## `src/ui/managemodrulesdialog.cpp`

### F198 — 🟡 Low · Functionality
**Location:** `src/ui/managemodrulesdialog.cpp:94-103`  
**Title:** Remove-rule uses view currentRow as index into rules_, which can desync if the table is ever sorted  
**Problem:** on_remove_rule_button_clicked erases rules_[currentRow]. refreshTable() inserts rows in rules_ order and the table is not sortable by default, so currentRow currently matches the vector index. This is fragile: enabling sorting or filtering on rules_table would silently remove the wrong rule. Not a current defect but an unguarded view-to-model index assumption.  
**Exact fix:** Store the rule identity (or vector index) in the table item's UserRole and resolve removal by that, or keep an explicit comment/assert that the table must remain unsorted.

---

## `src/ui/modconfigeditordialog.cpp`

### F105 — 🟡 Low · Security · ✓verified
**Location:** `src/ui/modconfigeditordialog.cpp:60-90, 141-159`  
**Title:** Config editor can follow symlinks and write outside staging via mod-supplied links  
**Problem:** scanForFiles mirrors AssetPreviewDialog and does not skip symlinks, and onSaveClicked writes via QSaveFile to the stored absolute path. A mod that ships a symlink named foo.ini pointing at a file outside its staging directory would let the user unknowingly read and then overwrite that external target through the editor (QSaveFile follows the link on commit). This turns a benign-looking 'edit this mod's config' action into an out-of-tree write.  
**Exact fix:** Exclude symlinks from the scan, or canonicalize each path and reject any that resolves outside staging_path_ before listing/saving.

---

## `src/ui/modlistmodel.cpp`

### F078 — 🟠 Medium · Functionality
**Location:** `src/ui/modlistmodel.cpp:121, 133, 135, 209, 217, 223, 243, 250`  
**Title:** data() uses .at() on maps keyed by mod id/group that can throw on inconsistent input  
**Problem:** Several data() paths call std::map::at(): mod_size_strings_.at(id) (121), manual_tag_map_.at(id)/auto_tag_map_.at(id) (133/135/243/250), group_versions_.at(group) (209), active_group_members_.at(group) (217), groups_.at(group) (223). These keys are only guaranteed populated for entries inserted in setModInfo. If active_mods_ contains a mod whose id/group wasn't inserted into a given map (e.g. group < 0 reaching active_index_role at 217, or partial/inconsistent ModInfo), at() throws std::out_of_range from inside a const data() override, which Qt does not expect and which crashes the app during painting.  
**Exact fix:** Use find() with a fallback (return QVariant() or empty) instead of at() in data(), or assert/skip the role when the key is absent. Especially active_index_role (217) and group_members_role (223) should check active_mods_[row].group >= 0 first.

### F023 — 🟠 Medium · Security · ✓verified
**Location:** `src/ui/modlistmodel.cpp:445-463, 510-519`  
**Title:** Thumbnail fetch issues outbound GET to mod-controlled URL (SSRF/privacy leak)  
**Problem:** thumbnailUrl derives the request URL from mod.remote_source, which is mod/network-supplied untrusted data. The host allow-listing uses substring checks (lower.contains("nexusmods") || ...contains("staticdelivery")) combined with an extension check. These are trivially bypassable, e.g. https://attacker.example/payload.png#nexusmods or https://nexusmods.attacker.example/x.png both pass, so on list render Limo silently issues an HTTP GET to an attacker-controlled host (SSRF / IP+activity disclosure / cache-poisoning of the on-disk thumbnail).  
**Exact fix:** Validate the parsed QUrl host against an exact suffix allow-list (e.g. host().endsWith(".nexusmods.com") / staticdelivery.nexusmods.com) and require https scheme, rather than substring matching on the whole URL. Reject non-https and unknown hosts.

### F199 — 🟡 Low · Functionality
**Location:** `src/ui/modlistmodel.cpp:114`  
**Title:** std::localtime is not thread-safe and ignores conversion failure  
**Problem:** data() formats install_time with std::localtime(&...), which returns a pointer to a shared static std::tm and is not thread-safe; it can also return nullptr for out-of-range time_t values, which std::put_time would then dereference. Although called on the UI thread today, the thumbnail callback and other async paths emit dataChanged that re-enters painting; a nullptr from a corrupt install_time would crash.  
**Exact fix:** Use localtime_r (or QDateTime::fromSecsSinceEpoch(...).toString) and handle a null/failed conversion by returning an empty string.

### F255 — 🟡 Low · Code Quality
**Location:** `src/ui/modlistmodel.cpp:345-359`  
**Title:** Size-string formatting has signed/unsigned comparison and fragile rounding  
**Problem:** In setModInfo, while(size > 1024 && exp < units.size()) compares int exp with size_t (units.size()), a signed/unsigned comparison. The loop uses > 1024 (not >=) and the subsequent last_size /= 1.024 plus per-digit extraction is convoluted and rounds inconsistently (e.g. values just above a power of 1024 render oddly). If exp ever reached units.size() the units[exp] access at line 359 would be out of bounds, though current input ranges keep exp <= 6.  
**Exact fix:** Use a size_t loop counter, clamp exp to units.size()-1, and replace the manual digit math with QLocale::formattedDataSize or a simple printf("%.1f %s")-style formatter.

---

## `src/ui/modlistview.cpp`

### F200 — 🟡 Low · Functionality
**Location:** `src/ui/modlistview.cpp:155, 175`  
**Title:** Selected-row count divides index count by columnCount with no guard against zero/partial selections  
**Problem:** getNumSelectedRows() returns selection().indexes().size() / model()->columnCount() and getSelectedRowIndices() iterates i += model()->columnCount(). These assume every selected row contributes exactly columnCount() indices. If columnCount() were ever 0 this is a divide-by-zero; more realistically, a selection that does not span full rows (possible via programmatic selection) yields an incorrect row count, silently miscounting selected mods used in batch operations.  
**Exact fix:** Compute selected rows from selectionModel()->selectedRows() (which returns one index per row) instead of dividing the flat index list by column count, and guard columnCount() > 0.

---

## `src/ui/movemoddialog.cpp`

### F201 — 🟡 Low · Functionality
**Location:** `src/ui/movemoddialog.cpp:22-25`  
**Title:** on_buttonBox_accepted re-emits modMovedTo with no completion guard  
**Problem:** Unlike the other dialogs in this unit, MoveModDialog::on_buttonBox_accepted() has no dialog_completed_ guard, so a double-accept (e.g. Enter then a second activation) could emit modMovedTo twice. The target value is also taken without re-checking hasAcceptableInput(); it relies solely on the OK button enable state.  
**Exact fix:** Add a one-shot completion guard and re-validate target_field->hasAcceptableInput() before emitting.

---

## `src/ui/nexusbrowserdialog.cpp`

### F106 — 🟡 Low · Security · ✓verified
**Location:** `src/ui/nexusbrowserdialog.cpp:155-156, 173-194`  
**Title:** Mod thumbnail URL is fetched/rendered without scheme validation  
**Problem:** mod.picture_url (network-controlled) is inserted into an <img src> in the QTextBrowser and also fetched via network_manager_.get() with no validation that it is an http/https URL. The src string itself is unescaped (a quote in the URL would break the attribute). While picture_url is normally a CDN URL, a malicious search response could supply a file:// or unexpected-scheme value or an attribute-breaking string.  
**Exact fix:** Validate the URL scheme (http/https only) and toHtmlEscaped() the URL before embedding it in the <img src> attribute; skip the request otherwise.

---

## `src/ui/nexusmoddialog.cpp`

### F008 — 🔴 High · Security · ✓verified
**Location:** `src/ui/nexusmoddialog.cpp:38-39, 152-155, 114-119, 146-151`  
**Title:** Untrusted NexusMods strings rendered as HTML without escaping (markup injection)  
**Problem:** setupDialog feeds network-controlled fields straight into RichText sinks with no toHtmlEscaped(): page.mod.description and file.description/changelog_html go through bbcodeToHtml() (which never escapes <, >, & and only rewrites BBCode tokens), and file.name/version/category_name plus the upload time are concatenated directly into QLabel RichText / QTextBrowser / QTextEdit. A malicious or compromised mod page can therefore inject arbitrary HTML (e.g. <img src="file:///home/user/.ssh/id_rsa">, oversized/styled content, fake links) that QLabel/QTextBrowser will parse and load. Unlike nexusbrowserdialog.cpp and nexuschangelogdialog.cpp, which correctly call toHtmlEscaped() on the same kind of data, this dialog does not.  
**Exact fix:** HTML-escape every plain-text field (name, version, category_name, the put_time string, external_virus_scan_url before using it as link text) with QString::toHtmlEscaped() before inserting into RichText, and make bbcodeToHtml() escape <, >, & in the raw input before applying token substitutions so embedded raw HTML in descriptions cannot survive.

### F079 — 🟠 Medium · Functionality
**Location:** `src/ui/nexusmoddialog.cpp:118`  
**Title:** std::localtime can return nullptr causing UB/crash on malformed upload timestamp  
**Problem:** file.uploaded_time comes from the network JSON. std::localtime returns a pointer to a shared static struct and returns nullptr for out-of-range time_t values; passing that nullptr to std::put_time is undefined behavior and can crash. std::localtime is also not thread-safe (the surrounding code uses QtConcurrent elsewhere).  
**Exact fix:** Check the std::localtime result for nullptr (fall back to an empty/'unknown' string) and prefer a thread-safe conversion (localtime_r) or QDateTime::fromSecsSinceEpoch for formatting.

### F024 — 🟠 Medium · Security · ✓verified
**Location:** `src/ui/nexusmoddialog.cpp:150`  
**Title:** Network-supplied virus-scan URL placed unescaped inside an href attribute  
**Problem:** external_virus_scan_url (from the NexusMods API) is interpolated directly into `<a href="...">` with no escaping or scheme validation. A value containing a double-quote breaks out of the attribute and injects arbitrary additional HTML/attributes into the label, and an attacker-controlled scheme (e.g. file://) is preserved verbatim. The same unescaped-href pattern recurs for the [url=...] BBCode token in bbcodeToHtml (line 234), where the captured URL is dropped into href without validation.  
**Exact fix:** Escape the URL with toHtmlEscaped() and validate the scheme (allow only http/https) before building the anchor; do the same for BBCode-derived [url]/[img]/[youtube] hrefs.

### F256 — 🟡 Low · Code Quality
**Location:** `src/ui/nexusmoddialog.cpp:52-53, 91-94`  
**Title:** files_widget children deleted with delete in a loop instead of layout-safe teardown  
**Problem:** setupDialog iterates ui->files_widget->children() and calls delete on each, then separately deletes/replaces the layout. children() includes the layout object and nested widgets in an order that is not guaranteed safe to delete while iterating; the pattern is fragile and duplicated (the empty-files early return at 56-61 repeats the layout swap). This is a maintenance/robustness smell rather than a confirmed crash.  
**Exact fix:** Use a single helper that removes and deletes all child widgets via the layout (e.g. qDeleteAll of layout items) and recreates the layout once, removing the duplicated swap logic.

### F257 — 🟡 Low · Code Quality
**Location:** `src/ui/nexusmoddialog.cpp:129, 143`  
**Title:** Signed/unsigned comparison and theoretical units[] over-index in size formatting  
**Problem:** The loop condition `exp < units.size()` compares int against size_t (signed/unsigned mismatch warning), and if size could exceed 1024^7 the loop would leave exp == 7 and index units[7] out of bounds at line 143. In practice size_in_bytes is a long (< 1024^7) so the over-index is unreachable, but the bound is off-by-one relative to the index use.  
**Exact fix:** Cast exp to size_t for the comparison and clamp exp to units.size()-1 before indexing units[exp].

---

## `src/ui/nexusnewsdialog.cpp`

### F202 — 🟡 Low · Functionality
**Location:** `src/ui/nexusnewsdialog.cpp:48-67`  
**Title:** News feed fetch uses a detached std::thread with no per-request cancellation or timeout  
**Problem:** refresh() spawns a detached std::thread running a blocking cpr::Get. The QPointer guard correctly prevents use-after-free on the dialog, but the request itself has no timeout set and cannot be cancelled; if the feed host hangs, the thread lingers until the network stack times out, and rapid refreshes (guarded only by is_loading_) are fine but a stuck request leaves the UI in 'Loading…' with the refresh button disabled.  
**Exact fix:** Set an explicit cpr timeout, and consider QtConcurrent + QFutureWatcher for consistency and easier lifetime management.

---

## `src/ui/passwordfield.cpp`

### F211 — 🟡 Low · UI
**Location:** `src/ui/passwordfield.cpp:16`  
**Title:** Show/hide password button has no accessible name and may render empty if theme icons are missing  
**Problem:** view_button_ is created with setText("") and only a tooltip; its icons come from QIcon::fromTheme("view-visible"/"view-hidden") (passwordfield.h:59-61). If the active icon theme lacks those names the QIcon is null, leaving a blank, label-less button. With no text and no accessibleName, screen readers also have no usable name for the toggle, an accessibility gap.  
**Exact fix:** Call view_button_->setAccessibleName("Show/Hide password") and provide a text or fallback icon when QIcon::fromTheme returns a null icon.

---

## `src/ui/repositoriesdialog.cpp`

### F080 — 🟠 Medium · Functionality
**Location:** `src/ui/repositoriesdialog.cpp:155-158, 217-219, 256-258`  
**Title:** Repository network calls (connect/listPackages/resolveDownloadUrl) run synchronously on the UI thread  
**Problem:** onAddRepoClicked, loadPackages and onInstallClicked construct an OmmRepository and call connect()/listPackages()/resolveDownloadUrl() directly on the GUI thread (only a WaitCursor is set). An unreachable or slow repository URL — which is user/network-controlled — will freeze the entire UI for the duration of the request/timeout with no cancel option, unlike the Nexus dialogs which use QtConcurrent.  
**Exact fix:** Move the repository network operations off the UI thread (QtConcurrent + QFutureWatcher as in nexusbrowserdialog), show a busy/loading state, and allow cancellation.

### F107 — 🟡 Low · Security · ✓verified
**Location:** `src/ui/repositoriesdialog.cpp:69-72, 88-90, 147-150`  
**Title:** Repository passwords persisted to disk as reversible base64 'obfuscation'  
**Problem:** OMM repository credentials are written to repositories.json as plain base64 (saveRepos) and decoded on load. This is trivially reversible and is functionally plaintext-at-rest for anyone with read access to the user's data dir. The code and the user prompt acknowledge this, but it remains a sensitive-data storage weakness; there is no integration with a secret store (e.g. QtKeychain / libsecret).  
**Exact fix:** Store secrets in the platform secret service (libsecret/KWallet via QtKeychain) instead of base64 in a world-readable JSON file; at minimum restrict the file permissions to 0600 and make clear the password is recoverable.

### F108 — 🟡 Low · Security · ✓verified
**Location:** `src/ui/repositoriesdialog.cpp:88-90`  
**Title:** OpenMW-repo password persisted to disk as reversible base64, not encrypted  
**Problem:** When save_password is enabled, the repository password is written to the config JSON using only QString::fromUtf8(config.password.toUtf8().toBase64()) (lines 88-90) and read back with fromBase64 (lines 68-71). Base64 is encoding, not encryption, so anyone with read access to the config file recovers the plaintext password instantly. The in-code comment even acknowledges this ("Passwords are stored base64-obfuscated only", line 68; the save-prompt warns it is "only base64", line 147). This is inconsistent with the project's own cryptography.cpp, which already provides AES-256-GCM encrypt/decrypt with an installation key used for Nexus API keys; the same facility is not applied here.  
**Exact fix:** Encrypt the repo password with the existing cryptography::encrypt (installation-key AES-256-GCM) used for the Nexus API key, instead of base64, or store it in a platform secret store (e.g. via libsecret/QtKeychain). At minimum tighten the file permissions and make the weak-obfuscation warning unmissable.

---

## `src/ui/restorepointsdialog.cpp`

### F203 — 🟡 Low · Functionality
**Location:** `src/ui/restorepointsdialog.cpp:45-52`  
**Title:** Delete restore point has no confirmation prompt for a destructive action  
**Problem:** on_delete_button_clicked immediately emits deleteRequested(index) and accepts the dialog with no confirmation, unlike the group-dissolve and save-delete flows which prompt. Deleting a load-order snapshot is irreversible; an accidental click removes it silently.  
**Exact fix:** Add a QMessageBox::question confirmation before emitting deleteRequested, consistent with other destructive actions in this unit.

---

## `src/ui/rootlevelcondition.cpp`

### F081 — 🟠 Medium · Functionality
**Location:** `src/ui/rootlevelcondition.cpp:72-93`  
**Title:** Unhandled std::regex_error from config-supplied pattern crashes the add-mod flow  
**Problem:** When matcher_type_ == regex, expression_regex.assign(expression_) (line 74) and std::regex_match (line 89) can throw std::regex_error for a malformed pattern. expression_ originates from steam_app_configs/<id>.json parsed in RootLevelCondition(const Json::Value&). detectRootLevel is called from AddModDialog::setupDialog (addmoddialog.cpp:388) with no surrounding try/catch, so a single bad pattern in any shipped/edited config aborts mod addition with an uncaught exception.  
**Exact fix:** Validate/compile the regex once in the constructor inside a try/catch (mark the condition invalid on failure), or wrap the assign/regex_match calls in detectRootLevel with a try/catch that treats a failed match as no-match and logs the error.

---

## `src/ui/savemanagerwidget.cpp`

### F082 — 🟠 Medium · Functionality
**Location:** `src/ui/savemanagerwidget.cpp:165-198`  
**Title:** Save deletion maps selected rows via stale UserRole index, can delete the wrong file after sorting  
**Problem:** refresh() stores each save's original index in name_item->setData(Qt::UserRole, row) once, while the table has sorting enabled (sortByColumn at construction). onDeleteClicked reads that UserRole value as the index into saves_. The UserRole stores the model row at populate time, but the table is then re-sorted, and saves_ is never reordered to match the view; the mapping happens to work only because UserRole equals the saves_ index. However any path where the row stored differs from the saves_ position (e.g. future column re-sorts that reuse name_item) risks deleting a different save than the one highlighted. The confirmation text ('the selected save file') gives the user no per-file detail to catch this.  
**Exact fix:** Resolve the save by a stable key (e.g. store the absolute path in UserRole and delete by path), and list the actual file name(s) in the confirmation dialog so the user can verify before a destructive, non-undoable delete.

---

## `tests/CMakeLists.txt`

### F083 — 🟠 Medium · Functionality
**Location:** `tests/CMakeLists.txt:34-43`  
**Title:** LOOT and OpenMW deployer tests are compiled out by default, leaving Bethesda/Morrowind load-order logic untested in normal builds  
**Problem:** test_lootdeployer.cpp and test_openmwdeployer.cpp are only added to TEST_SOURCES when LIMO_WITH_LOOT is ON. The top-level CMakeLists defaults LIMO_WITH_LOOT to OFF (CMakeLists.txt:16), and the fork's documented build keeps it OFF. So in the default/CI build these two files never compile or run: the LOOT plugins.txt/loadorder.txt parsing, the OpenMW openmw.cfg plugin+archive load-order generation, profile copy semantics (incl. the subtle addProfile condition the test comments at lines 104-117 explicitly reason about), and getModNames ordering all have effectively zero executed coverage. Regressions in this untrusted-file-parsing code (plugins.txt is user/mod-writable) would not be caught.  
**Exact fix:** Either provide a CI build matrix entry with LIMO_WITH_LOOT=ON so these tests actually run, or factor the pure parsing/load-order logic out from libloot so it can be unit-tested with LOOT OFF. At minimum, document that these tests are dark in the default configuration.

---

## `tests/test_deployer.cpp`

### F204 — 🟡 Low · Functionality
**Location:** `tests/test_deployer.cpp:11-19`  
**Title:** Several deployer tests rely on shared mutable DATA_DIR/app state without resetAppDir, making them order-dependent  
**Problem:** 'Mods are added and removed' (lines 11-19), 'Get mod conflicts' (122-136) and 'Get file conflicts' (138-149) construct a Deployer against DATA_DIR/source and DATA_DIR/app but never call resetAppDir(). DATA_DIR/app is a single shared mutable directory written by other test cases in this and other TUs. Catch2 does not guarantee a global cross-TU execution order, so these tests can observe leftover .lmmfiles/state from a prior test. They happen to pass today because they read source-dir mod folders, but the pattern is fragile and a reordering or added test could flip results.  
**Exact fix:** Call resetAppDir()/resetStagingDir() (or use a dedicated unique target dir) at the start of every test that constructs a Deployer over DATA_DIR/app, so each test is hermetic.

### F258 — 🟡 Low · Code Quality
**Location:** `tests/test_deployer.cpp:284-292`  
**Title:** Symlink assertion excludes files by filename (not path), so any file named 0/wasd/file.cfg anywhere escapes the check  
**Problem:** The symlink test skips entries whose filename equals `file.cfg`, `wasd`, `0`, or has extension `.lmmbak`, plus `.lmmfiles`/`.lmm_managed_dir`. The exclusions match on filename only, so ANY file named `0`, `wasd`, or `file.cfg` at any depth is exempt from the `is_symlink` requirement. If a deployer regression caused such a file elsewhere in the tree to be hard-linked/copied instead of symlinked, the test would not catch it. The exclusion list is also undocumented as to why each of those base files is legitimately not a symlink.  
**Exact fix:** Exclude by full relative path (the specific known non-overwritten files), not by bare filename, and add a comment explaining why each excluded entry is expected not to be a symlink.

---

## `tests/test_installer.cpp`

### F025 — 🟠 Medium · Security · ✓verified
**Location:** `tests/test_installer.cpp:9-104`  
**Title:** No test exercises path-traversal / zip-slip protection or any untrusted-archive extraction path  
**Problem:** The installer is the primary entry point for fully untrusted mod-archive contents, yet every test here extracts only the benign fixture mod0.tar.gz/mod1.zip/mod2.tar.gz. There is no fixture archive containing a `../` traversal entry, an absolute path, or a malicious symlink, so the ARCHIVE_EXTRACT_SECURE_NODOTDOT / SECURE_SYMLINKS / SECURE_NOABSOLUTEPATHS flags set in installer.cpp:685-686 have zero regression coverage — a future edit dropping those flags (re-introducing arbitrary file write outside the staging dir) would pass the suite. No test covers extractOmodArchive (a hand-rolled binary parser with its OWN manual `..`/absolute-path guard and integer offset arithmetic at installer.cpp:1261-1296), the RAR fallback, installPatch, detectInstallerSignature, pruneToSelection, or the no_extract path. These are exactly the untrusted-input sinks the audit flags.  
**Exact fix:** Add fixtures: an archive whose entries include `../escape.txt`, an absolute path, and a symlink escaping the dest; assert that after Installer::extract/install nothing is written outside the destination. Add an OMOD fixture (incl. one with a malformed/oversized length and a `..` entry) to pin extractOmodArchive's traversal guard and size checks.

---

## `tests/test_moddedapplication.cpp`

### F205 — 🟡 Low · Functionality
**Location:** `tests/test_moddedapplication.cpp:40-141`  
**Title:** No test covers deploy hooks (std::system) or pre/post-deploy command execution  
**Problem:** ModdedApplication::runHook hands user/profile-supplied hook strings to std::system verbatim (moddedapplication.cpp:1534-1553), one of the audit's named dangerous sinks. test_moddedapplication.cpp exercises install/deploy/group/uninstall but never sets or runs a deploy hook, so neither the exit-code handling nor the 'log and continue on failure' policy nor the verbatim-passthrough contract is tested. There is no regression guard ensuring hooks are not, for example, unexpectedly re-escaped or executed at the wrong time.  
**Exact fix:** Add a test that sets pre/post deploy hooks to a harmless command (e.g. writing a sentinel file) and asserts it ran and that a failing hook does not abort deployment, documenting that hook strings are intentionally trusted/verbatim.

---

## `tests/test_openmwdeployer.cpp`

### F259 — 🟡 Low · Code Quality
**Location:** `tests/test_openmwdeployer.cpp:76-82`  
**Title:** Load-order normalization loop silently skips plugins not found, which can mask a missing/renamed plugin  
**Problem:** The setup loop (also duplicated at lines 101-107 and 142-148) finds each expected plugin name in the live load order and swaps it into place, but guards with `if(iter != loadorder.end())`. If the deployer failed to surface a plugin (e.g. a parsing regression dropped d.EsP), the swap is silently skipped and the subsequent REQUIRE on getModNames/getNumMods may still pass for the remaining set ordering, masking the missing entry. The count check (REQUIRE getNumMods==size) partially guards this, but the silent-skip pattern is a latent assertion hole.  
**Exact fix:** Assert that each expected name is found (e.g. REQUIRE(iter != loadorder.end())) before swapping, so a missing plugin fails loudly at the point it disappears.

---

## `tests/test_reversedeployer.cpp`

### F260 — 🟡 Low · Code Quality
**Location:** `tests/test_reversedeployer.cpp:92-101`  
**Title:** Debug std::cout left in a committed test, polluting CI output  
**Problem:** 'Deployed files are ignored' prints 'Resetting directories...', 'Adding profile...', 'Adding mod...', 'Files deployed.' to std::cout during the run. This is leftover debugging noise in a committed test; it adds nothing to the assertions and clutters test output.  
**Exact fix:** Remove the std::cout debug statements (lines 92, 96, 98, 101).

---

## `tests/test_utils.cpp`

### F084 — 🟠 Medium · Functionality
**Location:** `tests/test_utils.cpp:8-37`  
**Title:** verifyDirsAreEqual does not compare file contents unless test_content=true; many deploy tests assert only the set of paths  
**Problem:** verifyDirsAreEqual defaults test_content=false (test_utils.h:19). With test_content=false getFiles records only relative path strings, so the check at lines 35-36 confirms only that both trees contain the same set of relative paths and the same count — file BYTES are never compared. The symlink deploy test (test_deployer.cpp:283), case-matching test (test_deployer.cpp:217-222), reverse-deployer, openmw, and bg3 verifications all pass false. Consequently conflict-resolution correctness at the byte level (did the winning mod's version actually land? does the symlink point at the right source?) is not asserted for those paths — a deployer bug that links/copies the wrong mod's file but with the right name would pass.  
**Exact fix:** Pass test_content=true wherever the winning content matters (especially the symlink and case-matching deploy tests), or add a dedicated content/symlink-target assertion. Resolve symlinks and compare the dereferenced bytes in the symlink test.

### F261 — 🟡 Low · Code Quality
**Location:** `tests/test_utils.cpp:16-23`  
**Title:** Content comparison concatenates path+bytes with no separator, allowing aliasing between distinct trees  
**Problem:** When get_contents=true, each entry is `relative_path` immediately followed by the file's bytes with no delimiter (line 16 builds the path, lines 19-21 append contents directly). Thus a tree with file `a/b` containing `c` produces the same comparison token as a tree with file `a/bc` containing nothing, or `a` containing `/bc`. UnorderedEquals over these tokens can therefore report two genuinely different directory trees as equal, weakening every content-comparing assertion.  
**Exact fix:** Insert an unambiguous separator (e.g. a NUL byte) between the relative path and the file contents, or compare path-set and per-file content as separate keyed structures.


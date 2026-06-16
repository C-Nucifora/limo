I now have all the evidence needed. Here is the brief.

---

# FS Mod Sources for Limo — Research Brief

Scope: how Farming Simulator mods are hosted/downloaded across sources, and what import flows are realistic for Limo. Verification status is marked **[Confirmed]** (I tested or directly read it) vs **[Uncertain]** (reported, not independently verified).

---

## 1. GIANTS Official ModHub

**URLs**
- Listing: `https://www.farming-simulator.com/mods.php?title=fs2025` (also FS22 = `fs2022`, etc.)
- Mod page: `https://www.farming-simulator.com/mod.php?mod_id=<ID>&title=fs2025`
- Special ToS: `https://www.farming-simulator.com/termsModHub.php`

**How hosting/download works [Confirmed]**
Each mod page has a `[DOWNLOAD]` button pointing at a GIANTS CDN. The direct-download URL pattern is stable and predictable:

```
https://cdn<N>.giants-software.com/modHub/storage/<10-digit-zero-padded-mod_id>/<Filename>.zip
```

Example (verified working): `https://cdn25.giants-software.com/modHub/storage/00312054/FS25_FollowMe.zip`
The same pattern is used by the community CLI tool `fsmm`, which pairs the mod page URL with a `cdnNN.giants-software.com/modHub/storage/...` link.

**Is there a public API / stable direct-download URL?**
- **Stable direct-download URL pattern: YES [Confirmed].** It's deterministic given the mod_id and the zip filename. The filename and exact `cdnNN` host are shown on the mod page, so you must scrape the mod page (or know them) to construct the URL reliably — the CDN number is not always guessable.
- **Documented public API: NO [Confirmed].** No official developer/REST API exists. The in-game ModHub uses an internal client protocol, not a documented public API. The website is plain PHP with query-string pagination/filters (`mods.php?title=fs2025&filter=latest&page=N`, filters like `mostDownloaded`, `harvesters`, `mapEurope`), so listings are scrapeable but there's no JSON API.

**Auth / captcha / browser session [Confirmed — this is the key finding]**
No login, no account, no captcha. **But the CDN enforces hotlink protection:**

| Request | Result |
|---|---|
| `curl` with no headers | **403 Forbidden** (146-byte error body) |
| `curl` with a browser `User-Agent` + `Referer: .../mod.php?mod_id=...` | **200 OK, full 140 KB zip downloaded** |

So downloads are gated only by a `User-Agent`/`Referer` check, not by authentication or session cookies. A normal HTTP client that sets a browser UA and a plausible Referer downloads the zip directly. This is a soft gate, not real auth.

**Terms of Service [Confirmed, with a caveat]**
The Special Terms (`termsModHub.php`) focus on: mods must be free (no commercial/gain), no redistribution via app stores, attribution required, and a broad license grant to GIANTS. They are **silent on automated downloading, scraping, or bots** — there is no explicit prohibition in that document. **[Uncertain]:** the general site Terms/robots.txt may address automated access; I did not separately audit those, and "silent" is not the same as "permitted." Hammering the CDN at scale could trip rate limiting or be viewed as abuse even absent an explicit clause.

**FEASIBILITY VERDICT for programmatic ModHub downloads: PARTIAL (technically feasible, modestly fragile, do as experimental/low-priority).**
- Why feasible: stable URL pattern; no auth/captcha; works today with a browser UA + Referer.
- Why partial/fragile: (a) you must scrape the mod page to learn the exact filename and `cdnNN` host before you can build the URL; (b) the hotlink gate (UA/Referer) is undocumented and can change without notice; (c) no API means version updates/new IDs require HTML scraping that breaks if GIANTS restyles the site; (d) ToS doesn't bless automation. Recommend treating it as best-effort/experimental, not a load-bearing feature.

---

## 2. Other notable third-party FS mod sources

All status notes for legitimacy are community-reported **[Uncertain]** unless stated.

| Source | Direct download? | API? | Reliability / legitimacy notes |
|---|---|---|---|
| **KingMods** (`kingmods.net`) | Yes, via the site's own pages | No public download API **[Confirmed]** — their GitHub org (`github.com/kingmodsnet`) only hosts Lua mod source, no downloader/API library | Most-trusted third party after the official hub; some content is scraped from ModHub but they typically keep original links **[Uncertain]**. No programmatic interface intended for external tools. |
| **modhub.us** (unofficial; not GIANTS) | Yes, but routed through interstitials/external hosts (e.g. sharemods-style) | No | Disputed legitimacy: re-hosts modders' work without clear permission; community warns of ad/clickbait pages and malware risk; GIANTS forum users call it piracy-adjacent **[Uncertain]**. Not recommended as a default. |
| **modhoster.com / .de** | Yes (own hosting) | No public API found | Long-running German site; generally considered legitimate hosting but no automation surface **[Uncertain]**. |
| **FS-UK (fs-uk.com)** | Yes | No | Established community site; manual download **[Uncertain]**. |
| **fs25.net / fs25mods / farmingsimulator25mods.com / fs25modhub.com etc.** | Yes, often via external mirrors | No | Large cluster of SEO/ad-driven aggregators; quality and safety vary widely; many re-host ModHub content behind ads **[Uncertain]**. Treat with caution. |
| **itch.io / GitHub / Discord** | Yes (itch.io and GitHub give real direct zip/release URLs) | GitHub has a real API (releases) **[Confirmed in general]** | Community-recommended "safe" off-hub sources. GitHub Releases in particular are clean, stable, scriptable direct-download URLs. |

Common thread: **no third-party FS site exposes a documented public download API.** The cleanest scriptable direct-download surface outside the official CDN is GitHub Releases (for mods distributed there).

---

## 3. Modpacks distributed as a unit

**Yes [Confirmed].** The official ModHub distributes modpacks as a single `.zip` like any other mod — same download mechanism, just large. Examples on FS25 ModHub:
- `FS25_Solek.zip` (~1223 MB, bundles multiple building/farm sets)
- `FS25_Geistal.zip` (~1351 MB)

So a "modpack" is not a special container format — it's one zip the game loads, identical import path to a single mod. Third-party sites also offer aggregated "mods packs" (e.g. modhub.us "Fed Mods Pack"), but those are unofficial bundles of someone else's zips, with the legitimacy caveats above.

**Implication for Limo:** no special modpack handling needed — a modpack is just a `.zip` import. (FS itself does not use a manifest-based multi-mod archive; each loadable unit is its own zip in the `mods/` folder.)

---

## 4. How existing FS tools handle download/install — lessons to borrow

I examined the active FS managers on GitHub:

- **FSG Mod Assistant** (`FSGModding/FSG_Mod_Assistant`) — **does NOT download** [Confirmed]. It's a local mod-folder switcher/validator: edits `gameSettings.xml` to point at different local `mods/` folders, detects bad filenames/conflicts, validates already-downloaded zips. Lesson: the mature, popular tool deliberately stays out of the download business and focuses on **local management, validation, and conflict detection** — exactly Limo's wheelhouse.
- **`rcbensley/fsmm`** (Python CLI) — **does download** [Confirmed], using the GIANTS CDN pattern. Its model: a text file pairing each mod-page URL with its `cdnNN.giants-software.com/modHub/storage/...` direct link, then plain HTTP GET. Lesson: programmatic ModHub download is real but it offloads URL discovery to the user (you paste the direct link). This sidesteps scraping fragility.
- **FS25_ModManager** (`NyboTV`, Electron/TS) — downloads only from **dedicated-server mod lists** (`http://server/mods`), syncing a client to a specific multiplayer server's mod set, fetching original filenames from detail pages. Lesson: there's a well-defined, legitimate "server mod sync" use case with a clean, documented format — arguably a better automation target than ModHub itself.
- **FSModManager** (`pchoiniere`) — FS19/22/25 local manager.

**Key install fact [Confirmed]:** FS loads mods from `Documents/My Games/FarmingSimulator<YEAR>/mods/` (Windows path; under Proton/Linux this lives in the game's Proton prefix). Install = drop the `.zip` there. No extraction, no manifest — the game reads zips directly. Limo's job is placing/symlinking zips into that folder and managing enable/disable + conflicts.

---

## 5. Recommendation — realistic import flows for Limo

**Tier 1 — Manual `.zip` import (ship this, always works) [Confirmed].**
User downloads any mod zip (from any source) in their browser, imports the `.zip` into Limo. Limo deploys it to the FS `mods/` folder (inside the Proton prefix). This is universal, safe, ToS-clean, and matches how every existing FS tool except `fsmm` operates. This should be the primary, well-supported path.

**Tier 2 — Semi-automated "paste a ModHub link" import (experimental, low-priority).**
Let the user paste a ModHub mod-page URL (`mod.php?mod_id=...`). Limo scrapes that page for the exact CDN zip URL + filename, then downloads it with a browser `User-Agent` and a `Referer` set to the mod page (this defeats the 403 hotlink gate — verified). Mark experimental because it depends on undocumented behavior (UA/Referer gate, HTML layout) and the ToS doesn't explicitly permit automation. Keep it opt-in, single-mod, gentle (no bulk crawling/ID enumeration), and fail gracefully to Tier 1.

**Tier 3 — GitHub Releases import (clean automation, optional).**
For mods hosted on GitHub, use the GitHub API/release asset URLs — stable, documented, scriptable. Lowest-risk automated download path. Worth supporting if effort allows.

**Explicitly out of scope / not recommended:**
- Building an integrated browser/scraper for modhub.us, fs25.net-type aggregators, KingMods, etc. — no APIs, ad/interstitial download flows, malware and re-hosting/legitimacy concerns. Point users to download manually instead.
- Bulk-crawling or ID-enumerating the official ModHub — fragile, abuse-adjacent, ToS-gray.

**Bottom line:** Make manual zip import excellent (Tier 1). Modpacks need no special handling — they're just large zips. The official ModHub "paste a link → auto-download" flow is **technically possible today** (no auth/captcha; UA+Referer defeats the 403) but **fragile and ToS-ambiguous**, so scope it as experimental/low-priority exactly as the user suspected. GitHub Releases is the one genuinely clean automated source if you want a reliable auto-download target.

---

**Primary sources**
- ModHub listing: https://www.farming-simulator.com/mods.php
- Example mod page (download button → CDN): https://www.farming-simulator.com/mod.php?mod_id=312054&title=fs2025
- Verified CDN direct URL: `https://cdn25.giants-software.com/modHub/storage/00312054/FS25_FollowMe.zip` (403 without headers, 200 with browser UA+Referer — tested)
- Special ToS: https://www.farming-simulator.com/termsModHub.php
- `fsmm` CLI (uses CDN pattern): https://github.com/rcbensley/fsmm
- FSG Mod Assistant (local-only manager): https://github.com/FSGModding/FSG_Mod_Assistant
- FS25_ModManager (server-sync downloader): https://github.com/NyboTV/FS25_ModManager
- KingMods: https://www.kingmods.net/en/ • their GitHub (no API): https://github.com/kingmodsnet
- modhub.us legitimacy discussion: https://steamcommunity.com/app/787860/discussions/0/1741102632993397086/ and https://forum.giants-software.com/viewtopic.php?t=202972

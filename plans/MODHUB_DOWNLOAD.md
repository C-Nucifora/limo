# Plan: ModHub & source downloads (low priority / experimental)

Research: [`research/02-fs-sources-modhub.md`](research/02-fs-sources-modhub.md).

The user flagged ModHub direct download as low-priority and uncertain. Research confirms: **technically feasible but fragile and ToS-ambiguous.** Make manual import excellent first; treat auto-download as opt-in/experimental.

## Tier 1 — Manual `.zip` import (the robust primary path) ✅

Works today for **every** source (GIANTS ModHub, KingMods, modhoster, FS-UK, OverTake, AssettoLand, GitHub, Discord). User downloads in their browser, imports the archive into Limo, Limo deploys it. Universal, safe, ToS-clean. This is how the most popular FS tool (FSG Mod Assistant) and AC workflows operate. **No new code** — this is Limo's existing import path; just document it per game.

## Tier 2 — ModHub "paste a link → download" (experimental, low priority)

**What works (verified):**
- Stable direct CDN URL pattern: `https://cdn<N>.giants-software.com/modHub/storage/<10-digit-zero-padded-mod_id>/<Filename>.zip`
  (e.g. `https://cdn25.giants-software.com/modHub/storage/00312054/FS25_FollowMe.zip`).
- **No login, no captcha, no cookies.** The CDN enforces only **hotlink protection**: a bare `curl` gets **403**; the same request with a browser `User-Agent` **and** a `Referer: https://www.farming-simulator.com/mod.php?mod_id=...` gets **200** and the full zip.
- The mod-page URL (`mod.php?mod_id=<ID>&title=fs2025`) shows the exact filename and `cdnNN` host; the listing (`mods.php?title=fs2025&...`) is scrapeable PHP (no JSON API).

**Why experimental / fragile:**
- You must **scrape the mod page** to learn the filename + `cdnNN` host before building the URL (the CDN number isn't reliably guessable).
- The UA/Referer hotlink gate is **undocumented** and can change without notice; HTML scraping breaks if GIANTS restyles.
- The Special ToS is **silent** on automation (not the same as permitted); general site terms/robots.txt not separately audited. **Do not bulk-crawl or ID-enumerate.**

**Proposed implementation (opt-in, single-mod, gentle):**
1. User pastes a ModHub `mod.php?mod_id=...` URL.
2. Limo fetches that page (browser UA), parses the download anchor → exact `cdn<N>.giants-software.com/modHub/storage/<id>/<file>.zip`.
3. Download with `User-Agent: <browser>` + `Referer: <mod page URL>` via the existing `cpr`/download path.
4. Hand the resulting `.zip` to the normal import → deploy flow. Fail gracefully back to Tier 1 with a clear message if the page layout or gate changes.

Plumbs into existing networking (`src/core/nexus/api.cpp` patterns, `cpr`) and the download/import pipeline (`src/ui/applicationmanager.cpp`, `downloadswidget`). Gate behind an "experimental" setting. **Respect a polite rate limit; no enumeration.**

## Tier 3 — GitHub Releases import (clean automation, optional)

For mods hosted on GitHub, use the documented Releases API / asset URLs — stable, scriptable, lowest-risk. Worth supporting generically (also benefits non-FS games). A "paste a GitHub release/repo URL" import.

## Explicitly out of scope

- In-app browser/scraper for `modhub.us`, `fs25.net`-type aggregators, KingMods, OverTake, AssettoLand — no APIs, ad/interstitial flows, malware & re-hosting/legitimacy concerns. **Point users to manual download.**
- Bulk-crawling / ID-enumerating ModHub — fragile and abuse-adjacent.

## Note: dedicated-server mod sync (possible future, clean)

A well-defined legitimate pattern exists: FS dedicated servers expose their mod set at `http://<server>/mods` (FS25_ModManager uses this). Syncing a client to a server's mod list is a cleaner automation target than ModHub itself — worth a separate future issue if multiplayer users ask.

## Work breakdown

1. Document Tier 1 manual import per game (FS/AC) — trivial. — folded into *Issue #229/#231*.
2. Tier 2 ModHub paste-a-link downloader, behind an experimental flag. — *Issue #233 (low priority)*.
3. Tier 3 GitHub Releases import (generic). — *Issue #233 (stretch) or its own issue*.

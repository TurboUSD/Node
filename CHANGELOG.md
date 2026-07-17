# TurboUSD Node — Changelog

Version history for the TurboUSD Node firmware (ESP32-S3 / SenseCAP Indicator D1)
and its companion web + backend. The ESP32 image version lives in
`firmware-esp32/platformio.ini` (`FIRMWARE_VERSION`) and is what OTA compares
against. Older lines are summarized; from here on, entries are detailed.

## Web/backend — July 2026 (no firmware change)

Communities ("projects"):
- Node settings (Profile section) gain a Projects list: one unified search bar that accepts a token ticker (DexScreener) or a pasted OpenSea/Satflow/ordinals link. Up to 10 per node, one marked ★ favorite. Saved instantly via three new Edge Functions (add-node-project / remove-node-project / set-favorite-project) and a new node_projects table.
- Block tiles on `/` and `/node` redesigned: winner's name sits where the reward figure was, their ★ favorite community sits where the name was.
- Block pages add a "Part of" row: the winner's communities as tags, favorite highlighted first, each linking to a new `/community/[key]` page with aggregate stats and a member table.
- New "By Communities" leaderboard (blocks mined by each community's members); the leaderboard grid grows to three columns on wide screens and a third toggle tab on mobile.
- Requires re-running `backend/sql/schema.sql` (new table + updated public_mining_feed + two new views) and deploying the three new functions.

## 0.4.1 — July 2026

Communities on the device network screen:
- Mined block tiles now match the web: the winner's node name is the centered headline (bright green, prominent) where the ₸ reward used to be, and their favorite community sits below it where the name was — with "(+N)" showing how many more communities the winner belongs to.
- Tapping a node name anywhere on the device (block winners, leaderboard rows, your own node) now shows a "Part of  SYMBOL (+N)" line under Uptime in the info popup.
- The web block-card project line also shows the "(+N)" extra-community count, and the favorite tag now appears next to the node name on the map popup, the map detail overlay, and the full node profile (which gains a "Part of N communities" section). The ★ glyph was dropped everywhere in favor of the highlighted green tag style.
- Requires re-running the community display views (`public_mining_feed` gains `winner_project_count`; `public_node_directory` gains `fav_project_name` / `fav_project_symbol` / `project_count`) — see `backend/sql/2026-07-17-communities-display.sql`.

## 0.4.0 — July 2026

Price alerts, on tickers and NFTs:
- Edit mode now shows a bell on every ticker card (market-cap alert) and NFT cell (floor alert). Yellow means armed. Also configurable from the web setup page.
- When one triggers, the device rings TURBOALARM with the reason, e.g. $CLAWD > $40M. Alerts are one-shot and sync both ways with the web.
- Also: chart downloads pace themselves to avoid rate limits, cleaner device settings text, and less log noise.

## 0.3.6 — July 2026

Stability optimization:
- Fixes occasional spontaneous reboots under heavy load: image decoding and disk-cache cleanup now yield CPU time, and the task watchdog is more tolerant.
- Internal hardening: larger worker stacks, safer cross-core data handling, JSON parsing moved to PSRAM.

## 0.3.5 — July 2026

Minor fixes and polish:
- DRB: "Grok Wallet" column (balances + total), token logo in the header, and a
  market-cap chart axis for all tickers.
- Home background now accepts WEBP and oversized images (via image proxy);
  legibility shadows sit behind each of the clock, date and alarm.
- ₸USD stats restored (supply / price / burned % / treasury).

## 0.3.4 — July 2026

- **Ticker Stats screen** (formerly "TurboUSD Stats"): now a generic single-token
  stats page. The backend `ticker-stats` function computes every field; the
  device just paints them. ₸USD is the default; pick any token from the web
  setting or the on-device footer picker (a token search, to the right of
  "Network: N nodes"). Custom tokens (e.g. DRB) get tailored fields; everything
  else shows price, market cap, 24h volume and liquidity from DexScreener. The
  selected token's logo shows in the centre of the stat grid (₸USD has none).
- **Home background image:** a web setting paints an image behind the clock
  (1:1 looks best), with a soft shadow behind the clock/date/alarm so they stay
  readable. Leave it empty for plain black.
- **Ticker Screener:** the right-hand mini charts on collapsed cards no longer
  flash on every background refresh — an unchanged list reload now reuses the
  existing cards instead of rebuilding the whole tree.
- **In-app changelog:** the OTA "update available" dialog gained a "What's new"
  link (shows this release's notes with a Back button), and the web setup page
  shows the changelog when it detects a newer version.

## 0.3.0 – 0.3.3 — the 0.3 line

Iterative features and stabilization on top of the IDF 5.5 base (summarized;
detailed tracking starts at 0.3.4):

- NFT gallery: image pipeline hardening, disk cache, wallet-scan fixes, caption
  band, ordinal resolve, per-cell carousel.
- Token Screener charts audit, screen auto-carousel, panel bounce-buffer flicker
  fix, disk-cache watchdog fix, location privacy, WiFi log viewer, swipe/tap
  guard, alarm-trigger and mining-stall fixes.

## 0.2.0 — IDF 5.5 / Arduino-ESP32 3.x

Moved the firmware onto the pioarduino platform (ESP-IDF 5.5, Arduino-ESP32 3.x).
A large migration plus the networking/OTA work that followed: buffered HTTPS body
reads, TLS-RAM gating, a single loop task owning TLS fetches, and the OTA update
flow (GitHub release → Supabase register → device downloads).

## 0.1.0 — Initial release

First working build: clock/home, live ₸USD data, US-debt and inflation screens,
node/network screen, WiFi setup, heartbeat + registration against the backend.

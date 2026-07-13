# TurboUSD Node — Changelog

Version history for the TurboUSD Node firmware (ESP32-S3 / SenseCAP Indicator D1)
and its companion web + backend. The ESP32 image version lives in
`firmware-esp32/platformio.ini` (`FIRMWARE_VERSION`) and is what OTA compares
against. Older lines are summarized; from here on, entries are detailed.

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

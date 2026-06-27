# TurboUSD Node

TurboUSD Node is an open-source project that turns a physical desktop device into a participant in a decentralized token network. The device — a [SenseCAP Indicator D1](https://wiki.seeedstudio.com/SenseCAP_Indicator_D1_Overview/) running custom firmware — doubles as a clock, alarm, financial dashboard, and crypto screener, while simultaneously acting as a mining node in the TurboUSD network. Every hour, one online node is selected as the block winner and credited ₸USD rewards.

The project is fully open source and welcomes contributions at every layer: firmware, backend, and web.

---

## How it works

1. **Device comes online** — after flashing the firmware and connecting to WiFi, the device registers itself with the backend and begins sending regular heartbeats.
2. **Block mining** — every hour, a server-side cron job fetches the latest Base blockchain block hash (public, unpredictable randomness) and uses it to deterministically select one online node as the winner. The selection is fully auditable: anyone can verify `BigInt(blockHash) % candidateCount = winnerIndex`.
3. **Rewards** — verified nodes accumulate ₸USD rewards, paid out to their configured Base wallet address.
4. **Transparency** — every block, its timestamp, winner, randomness source, and candidate count are public at `network.turbousd.com`.

---

## Hardware

The device is a **[SenseCAP Indicator D1](https://wiki.seeedstudio.com/SenseCAP_Indicator_D1_Overview/)** by Seeed Studio. It was chosen because it ships with everything needed in one compact enclosure:

<p align="center">
  <img src="https://files.seeedstudio.com/wiki/SenseCAP/SenseCAP_Indicator/SenseCAP_Indicator_2.png" width="70%" alt="SenseCAP Indicator D1 — back view: top Button, Grove ADC and Grove IIC ports, USB-C" />
  <br /><br />
  <img src="https://files.seeedstudio.com/wiki/SenseCAP/SenseCAP_Indicator/SenseCAP_Indicator_3.png" width="70%" alt="SenseCAP Indicator D1 — edge view: internal (RP2040 boot) button, USB-C, microSD card slot, antenna connector" />
</p>

| Component | Details |
|---|---|
| ESP32-S3 | Dual-core 240 MHz, 8 MB PSRAM — runs the main firmware |
| RP2040 | Co-processor — drives the buzzer and reads the Grove AHT20 temp/humidity sensor, reporting both to the ESP32-S3 over UART (the SD card is also wired to it, not yet used by the firmware) |
| Display | 3.95" RGB 480×480 touchscreen (ST7701S driver, FT6336U touch controller) |
| IO expander | TCA9535 — routes LCD control lines and backlight |
| Connectivity | WiFi 802.11 b/g/n, Bluetooth 5.0, USB-C |

No additional hardware modifications are required. The firmware communicates with the RP2040 over UART (115200 8N1) using a fixed 3-byte frame — `[0x7E][command][checksum]`, where the checksum is `0x7E XOR command`. See `firmware-rp2040/PROTOCOL.md` for the full command set.

### Buttons

The device has two buttons, on opposite edges:

- **Top button** (the regular push button) — handled by this firmware:
  - **Short press** — turn the screen off / wake it up. The node keeps running and mining in the background while the screen is off.
  - **Long press (3 s)** — sleep ("power off"): the screen turns off and the device enters light sleep; press the button again to wake.
  - There is intentionally **no factory-reset shortcut** on the button, so the firmware can't be wiped by accident. (Stock Seeed firmware reset the device on a 10 s hold — we deliberately dropped that.)
- **Bottom pinhole button** (next to the USB-C port, press with a paperclip/needle) — this is the **RP2040 BOOTSEL** button, used only when flashing the RP2040: hold it while plugging in USB to expose the `RPI-RP2` drive.

<p align="center">
  <img src="docs/flash-uf2-reference.png" width="70%" alt="Flashing the RP2040: hold the bottom pinhole button while connecting USB, then drag the .uf2 onto the RPI-RP2 drive" />
</p>

---

## Device screens

The device has **7 screens** navigated by horizontal swipe gestures. The default order is shown below; it can be customised per-device from the web setup page (Home is always first). Each screen's header contains the TurboUSD logo (tap → Home), the current date (tap → calendar popup), and a bell icon (tap → alarm picker). The footer has a QR code that opens the device's web setup page.

### 1 — Home
The default screen. Displays:
- **Clock** — current time in large digits, date
- **Alarm** — status shown below the time; tap to open the alarm picker (time, days of the week, on/off toggle, volume 1–5). The alarm fires the RP2040 buzzer at the configured volume level.
- **Temperature & humidity** — ambient readings from the Grove AHT20 sensor on the RP2040, polled over UART (shows `--` until the RP2040 firmware is flashed and an AHT20 is connected)
- **Weather** — current conditions fetched from the network

### 2 — TurboStats
Live TurboUSD token data sourced from the Uniswap V3 TUSD/USDC pool on Base. Displays:
- Current price in USD
- 24h change %
- Treasury balance
- OHLCV candlestick chart (configurable time range)

### 3 — Token Tickers
A configurable live crypto price screener. Each node has its own ticker list stored in the backend and synced on load. Features:
- Search tokens by name or symbol, or paste a contract address directly (routes to DexScreener's tokens API)
- Each ticker shows price, 24h change %, and liquidity
- ▲/▼ arrows indicate direction
- Add and remove tokens at any time from the screen or the web setup page

### 4 — Debt
US national debt tracker. Shows the live total in USD with a real-time increment counter (debt grows ~$26,000/second), plus a historical chart. Tap the range button to select the chart period: 5Y, 10Y, 20Y, 30Y, 50Y, or 75Y.

### 5 — Inflation Game
An interactive purchasing-power calculator. Enter any dollar amount and a start year; the screen shows how much has been lost to inflation since then. Tap the year range button to change the comparison window. A concrete illustration of monetary debasement over time.

### 6 — NFT Gallery
Displays NFTs fetched from the OpenSea API. Two source modes, configurable from the web setup page:

- **By Wallet** — auto-fetches all NFTs held by a configured wallet address (default mode)
- **Manual Picks** — show a hand-curated list of specific NFTs by pasting their OpenSea URLs (`opensea.io/item/chain/contract/tokenId`). Manual picks take priority over the wallet when active.

Additional features:
- **Grid sizes**: 1×1 (single large tile), 2×2 (4 tiles), or 3×3 (9 tiles)
- **Carousel**: each cell automatically cycles through multiple NFTs in the same collection
- **Slideshow**: auto-advances all cells on a configurable timer (default 10 s; set 0 to disable)
- **Spam filtering**: collections with a floor price of 0 are excluded automatically
- **Sorting**: NFTs ranked by collection floor price — highest value first
- **Caching**: data is cached for 30 minutes on the device to avoid hitting OpenSea rate limits

On first open, the device prompts for a wallet address (0x…). A different wallet can be used for NFTs and for reward payouts.

### 7 — Network
The node's own status and mining panel. Displays:
- Node name, code, verification badge (✓ once verified, ⏳ while pending)
- Uptime % and total ₸USD earned
- Live mining feed: recent mined blocks sliding left with winner names and rewards, plus the pending block's countdown ring
- Prompt to complete the profile (wallet + display name) if missing

Device settings — display preferences (°C/°F, date format, time format), alarm, and the setup QR code — are accessible from the **footer of every screen** by tapping the QR icon, which opens the settings popup in-place.

---

## Architecture

```
backend/              Supabase Edge Functions (TypeScript / Deno) + SQL schema comments
firmware-esp32/       ESP32-S3 firmware: WiFi, 7 LVGL screens, OTA, API client
firmware-rp2040/      RP2040 firmware: buzzer alarm, 3-byte UART command protocol from ESP32-S3
web/                  Next.js app (Vercel): node setup, setup wizard, network explorer
.github/workflows/    CI: builds both firmwares, publishes OTA releases
```

### Backend (Supabase)

The backend is a single Supabase project. It provides:

- **PostgreSQL database** — nodes, heartbeats, reward balances, mining blocks, firmware releases, OHLCV history, US debt history, node ticker configs.
- **Edge Functions** (TypeScript / Deno):
  - `register-node` — creates a new node record on first boot; auto-detects country/city/lat/lng from the device's public IP (city-level precision)
  - `heartbeat` — receives periodic pings, marks nodes online/offline, returns the full config payload so devices sync any web-changed settings on every check-in; backfills geolocation for nodes registered before geo was added
  - `mine-block` — hourly cron: selects the winner, issues reward, creates the next block
  - `update-node-config` — web-triggered config updates (profile, preferences, alarm volume, NFT settings including manual pinlist, screen order)
  - `resolve-nft` — resolves OpenSea metadata (name, image, floor price) for manual NFT picks by URL
  - `submit-verification` — accepts the X post URL for manual review
  - `search-tokens` / `add-node-ticker` / `remove-node-ticker` — token screener management
  - `sync-ohlcv-history` — daily: fetches TUSD OHLCV from Uniswap V3 on Base
  - `sync-debt-history` / `debt-history` — daily sync + public read of US debt data
  - `latest-firmware` — returns the latest firmware version for OTA checks
  - `rewards-payout` / `confirm-payout` — batch reward disbursement
- **Scheduled cron jobs** — mining every hour, data syncs daily.
- **Public views** — `public_node_directory`, `public_mining_feed`, `node_ticker_config` expose read-only data using the anon key. Wallet addresses and sensitive fields are only accessible via the service role key.

### Firmware (ESP32-S3)

Built with the Arduino + ESP-IDF stack via PlatformIO (espressif32 @ 6.x). Key modules:

- **`main.cpp`** — boot sequence, WiFi provisioning, OTA check loop, heartbeat timer, alarm trigger
- **`ui/ui_manager.h`** — LVGL orchestration: display/touch init, owns all 7 screen instances, swipe navigation with user-configurable order (persisted in NVS), alarm bell updates
- **`ui/screen_*.h`** — one header per screen; each is a self-contained class with `build()`, `onShow()`, and background-fetch via FreeRTOS tasks + 100ms LVGL poll timer (to keep rendering on core 1)
- **`api_client.h`** — HTTP helper wrapping `HTTPClient`; all backend calls live here. `sendHeartbeat()` parses the config payload returned by the server and applies any changed fields (temp unit, alarm time/volume, NFT settings, screen order) to NVS automatically.
- **`storage.h`** — thin NVS wrapper; all flash reads/writes go through this class. Covers WiFi credentials, alarm (hour/minute/days/enabled/volume), display preferences, NFT gallery settings, NFT manual pinlist, and screen order.
- **`rp2040_link.h`** — UART driver for the ESP32↔RP2040 link; `playAlarm(volume)` sends the appropriate volume command (0x11–0x15)
- **`config.h`** — compile-time constants: endpoint URLs, API keys, NVS key names, screen count

The display bring-up sequence (ST7701S SPI init, TCA9535 IO expander, FT6336U touch, ESP-IDF RGB panel) is fully implemented in `initDisplayAndTouch()` inside `ui_manager.h`, ported from Seeed's reference firmware.

### Firmware (RP2040)

The RP2040 co-processor runs a C firmware (Arduino core) that:
- Controls the buzzer for the alarm with **5 volume levels** (duty-cycle PWM via `analogWrite`). Legacy command `0x01` plays at volume 2; volume-aware commands `0x11–0x15` select levels 1–5 explicitly, maintaining backward compatibility during OTA rollouts.
- Reads ambient sensors (BMP280 temperature/pressure)
- Manages the SD card
- Communicates with the ESP32-S3 over UART — see `firmware-rp2040/PROTOCOL.md` for the 3-byte frame format and full command table

### Web app

A Next.js app deployed to Vercel at `network.turbousd.com`:

- **`/`** — live network dashboard: animated block ticker with circular mining countdown, node leaderboards (rewards and uptime), network growth sparkline, Telegram alert banner, **geolocated node map** (Leaflet, city-level dots, click for node card). Also a **PWA** — shows an "Add to Home Screen" banner on mobile for a native-app experience; remembers your node code across visits.
- **`/setup`** — onboarding wizard: one-click browser-based firmware flash via [ESP Web Tools](https://esphome.github.io/esp-web-tools/), WiFi setup instructions, device registration guide
- **`/setup/[nodeCode]`** — per-node settings page (also linked from the device QR code):
  - Profile: display name, bio, country/city
  - Rewards & Identity: Base wallet address (for ₸USD payouts), X/Twitter handle
  - Alarm: time picker + day-of-week selector + **volume slider (1–5, default 2)**
  - Display preferences: °C/°F, date format, time format
  - NFT Gallery: **By Wallet** (auto-detect from address) or **Manual Picks** (paste OpenSea URLs one by one); grid size, carousel, slideshow interval
  - Screen order: drag-and-drop to reorder the 7 device screens (Home always fixed first)
  - Verified badge: submit X post URL for manual verification
- **`/block/[number]`** — block explorer: timestamp, winner, reward, candidate count, Base block hash used as randomness source

### Telegram bot (`@ami9000_bot`)

An async Python bot (`ami9000/telegram/bot.py`) that users interact with for mining alerts:

- `/mynode YOUR_CODE` — links a Telegram account to a node; sends alerts whenever that node wins a block
- Mining alerts fire automatically when `mine-block` runs and a linked user's node wins

---

## Getting started

### Setting up your device

This is the recommended path for most users — no backend or code required.

**1. Get a SenseCAP Indicator D1**

Available from [Seeed Studio](https://www.seeedstudio.com/SenseCAP-Indicator-D1-p-5643.html) and various resellers. No other hardware is required.

**2. Flash the firmware**

Go to **[network.turbousd.com/setup](https://network.turbousd.com/setup)** in Chrome or Edge (desktop only — Web Serial requires a Chromium-based browser):

1. Click **Flash firmware** and connect the device via USB-C.
2. Select the correct serial port when prompted.
3. The browser flashes the latest firmware automatically. Takes about 60–90 seconds.

**3. Connect to WiFi**

After flashing, the device boots into provisioning mode:

1. On your phone or laptop, connect to the WiFi network named `TurboUSD-Setup-XXXX`.
2. A captive portal opens automatically (if it doesn't, open `192.168.4.1` in a browser).
3. Enter your home WiFi credentials and tap Save.
4. The device reboots and connects to your network.

**4. Complete your profile**

Scan the QR code on the device's home screen footer, or go to `network.turbousd.com/setup/YOUR_CODE`. Fill in:

- **Node name** — displayed publicly on the network
- **Base wallet address** — where your ₸USD rewards will be sent (required for payouts)
- **Country** — shown on the public network page

These are saved immediately; the device picks up changes on its next heartbeat cycle (within a few minutes).

**5. Get mining alerts on Telegram**

Open [@ami9000_bot](https://t.me/ami9000_bot) and send: `/mynode YOUR_CODE`

You'll receive a notification whenever your node wins a block.

**6. Get verified**

To receive ₸USD reward payouts, your node needs to be verified:

1. Post a short video on X tagging [@TurboUSD](https://x.com/turbousd) showing your device screen with your node name visible.
2. Paste the X post URL into the Verified badge section of your setup page.
3. The team reviews manually — usually within a couple of days.

---

### Running your own instance

If you want to self-host the full stack (your own Supabase, your own domain), follow these steps.

#### 1. Backend (Supabase)

1. Create a [Supabase](https://supabase.com) project.
2. Set up the database schema. The repo doesn't currently include a single migration file, but each Edge Function file header contains the `ALTER TABLE` statements it needs as SQL comments. Open each function in `backend/functions/` and run the SQL comments at the top to create the required columns. The main tables are `nodes`, `mining_blocks`, `node_tickers`, and `firmware_releases`. Key columns added beyond a bare-minimum schema:
   - `nodes`: `screen_order text`, `nft_pinlist text`, `alarm_volume smallint DEFAULT 2`, `lat double precision`, `lng double precision`
   - `public_node_directory` view must expose `lat` and `lng` for the network map (see migration comment in `register-node/index.ts`)
3. Deploy Edge Functions using the [Supabase CLI](https://supabase.com/docs/guides/cli):
   ```bash
   supabase link --project-ref YOUR-PROJECT-REF
   supabase functions deploy register-node
   supabase functions deploy heartbeat
   supabase functions deploy mine-block
   supabase functions deploy update-node-config
   supabase functions deploy submit-verification
   supabase functions deploy search-tokens
   supabase functions deploy add-node-ticker
   supabase functions deploy remove-node-ticker
   supabase functions deploy latest-firmware --no-verify-jwt
   supabase functions deploy sync-debt-history
   supabase functions deploy debt-history --no-verify-jwt
   supabase functions deploy sync-ohlcv-history
   supabase functions deploy ohlcv-history --no-verify-jwt
   supabase functions deploy rewards-payout --no-verify-jwt
   supabase functions deploy confirm-payout --no-verify-jwt
   supabase functions deploy resolve-nft
   ```
4. Note your project's URL, anon key, and service role key for the next steps.
5. Enable the `pg_cron` extension in Supabase (Database → Extensions) and schedule `mine-block` to run every hour.

#### 2. Web app

```bash
cd web
cp .env.example .env.local   # set NEXT_PUBLIC_SUPABASE_URL and NEXT_PUBLIC_SUPABASE_ANON_KEY
npm install
npm run dev                  # http://localhost:3000
```

Deploy to [Vercel](https://vercel.com) by importing the repo. Set the Root Directory to `web` and add the two environment variables in Vercel's project settings.

#### 3. Firmware

The easiest way to flash a device is the browser-based tool at your deployed `/setup` page. For local development:

```bash
cd firmware-esp32
# Edit include/config.h — set your Supabase URL, anon key, and other endpoints
pio run                      # build
pio run --target upload      # flash over USB-C
pio device monitor           # serial logs
```

CI automatically builds and publishes firmware releases when changes are pushed to `main` under `firmware-esp32/` or `firmware-rp2040/`. Devices pick up updates via OTA automatically — no re-flashing needed.

Required GitHub repository secrets: `SUPABASE_URL`, `SUPABASE_ANON_KEY`, `SUPABASE_SERVICE_ROLE_KEY`.

---

## Extending the project

The codebase is designed to make it straightforward to add new screens, data sources, or backend functions. Some patterns to know:

### Adding a new device screen

1. Create `firmware-esp32/include/ui/screen_myscreen.h`. Follow the pattern of any existing screen (e.g. `screen_node.h`): a class with `build(lv_obj_t* parent, ...)`, `onShow()`, and a `SharedHeaderRefs header` member.
2. Add the new entry to the `ScreenId` enum in `ui_manager.h` (increment `COUNT`).
3. Add a member instance, build call, `attachSwipeGesture`, and `_wireAlarmIcon` call in `buildAllScreens()`.
4. If the screen fetches data from the network, follow the FreeRTOS pending-result pattern: a background task on core 0 writes to a static `_pendingResult` struct; a 100 ms LVGL timer on core 1 polls it and applies results (keeps LVGL rendering smooth).
5. Add the screen label to `SCREEN_LABELS` in the web setup page so it appears in the drag-to-reorder list.

### Adding a new backend Edge Function

Edge Functions live in `backend/functions/`. Each is a single `index.ts` file. The Supabase client uses the service role key server-side (never exposed to devices). Use the anon key for public read-only endpoints. Add `--no-verify-jwt` to the deploy command for endpoints that devices call without a user JWT.

### Adding new per-device settings

1. Add the column to the DB (`ALTER TABLE nodes ADD COLUMN ...`).
2. Add the field to `update-node-config/index.ts` (body type → validation → updates map → select).
3. Add the NVS key to `config.h` and a getter/setter pair to `storage.h`.
4. Add the UI control to the relevant section in `web/app/setup/[nodeId]/page.tsx`.
5. The device picks up new values on the next heartbeat/config sync cycle.

---

## Contributing

Contributions are welcome at any layer. Some areas where help is particularly valuable:

- **New screens** — ideas include weather, sports scores, stock portfolio, RSS feeds, sleep tracker
- **NFT image rendering** — the NFT screen fetches metadata from OpenSea but displays placeholder tiles; full JPEG decode is wired (`LV_USE_SJPG=1` or TJpgDec) but needs integration with the downloaded image URLs
- **RP2040 firmware** — sensor data (temperature, humidity) forwarding to ESP32-S3 screens; SD card logging
- **Backend** — new data pipelines, automated reward disbursement, on-chain verification
- **Web** — mobile experience, block explorer improvements, social features
- **Translations** — the device UI is English only

Please open an issue before starting significant work so we can coordinate. All contributions are under the same open-source license as the rest of the project.

---

## Links

- Network dashboard: [network.turbousd.com](https://network.turbousd.com)
- Device setup: [network.turbousd.com/setup](https://network.turbousd.com/setup)
- TurboUSD: [turbousd.com](https://turbousd.com)
- Telegram bot: [@ami9000_bot](https://t.me/ami9000_bot)
- X: [@TurboUSD](https://x.com/turbousd)

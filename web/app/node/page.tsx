'use client'

// app/node/page.tsx — network.turbousd.com/node
//
// Product page for the TurboUSD Node device: what it is, what it does, why
// you want one on your desk. The hero shows the real device photo with a
// simulated NodeOS screen cycling through the actual firmware screens, and
// the page ends with a live slice of the network (stats, block ticker,
// compact map) that links back to the main network page.

import { useEffect, useState, useMemo, useRef, useCallback } from 'react'
import Link from 'next/link'
import { supabase } from '@/lib/supabase'
import SiteHeader from '@/components/SiteHeader'

// ── Brand tokens (same palette as the network page) ───────────────────────────
const C = {
  green:   '#43e397',
  onGreen: '#000000',
  blue:    '#5b8dee',
  yellow:  '#ffcf72',
  red:     '#ff6b6b',
  bg:      '#000000',
  card:    '#0c0c0c',
  surface: '#141414',
  border:  '#1c1c1c',
  text:    '#e8e8e8',
  muted:   '#6e7280',
}

const BLOCK_INTERVAL_MS = 60 * 60 * 1000
const SEEED_STORE_URL   = 'https://www.seeedstudio.com/SenseCAP-Indicator-D1-p-5643.html'
const GITHUB_URL        = 'https://github.com/turbousd/node'
// Hero: front shot of the SenseCAP Indicator D1; NodeOS is composited onto
// the panel area below (SCREEN_BOX). Secondary photo for the hardware section.
const DEVICE_IMG      = 'https://img.fruugo.com/product/8/79/2398495798_max.jpg'
// Hardware section: same two annotated views the README shows, back (ports)
// on top, edge (button/USB/microSD/antenna) below. Click → fullsize popup.
const DEVICE_IMG_BACK = 'https://files.seeedstudio.com/wiki/SenseCAP/SenseCAP_Indicator/SenseCAP_Indicator_2.png'
const DEVICE_IMG_ALT  = 'https://files.seeedstudio.com/wiki/SenseCAP/SenseCAP_Indicator/SenseCAP_Indicator_3.png'
// Where the physical screen sits WITHIN the hero photo, in % of the image.
// Tweak these four numbers if the overlay drifts off the panel. The box is
// intentionally a hair larger than the glass so its black edge melts into
// the bezel and small offsets are invisible.
const SCREEN_BOX = { left: '11%', top: '11%', width: '78%', height: '78%' }

// ── Types (subset of the network page's) ──────────────────────────────────────
interface NodeRow {
  node_code:         string
  display_name:      string
  is_verified:       boolean
  is_online:         boolean
  total_tusd_earned: number
  blocks_won:        number
  created_at:        string
  lat:               number | null
  lng:               number | null
}

interface MiningBlock {
  block_number:        number
  reward_tusd:         number
  winner_display_name: string | null
  mined_at:            string | null
  created_at?:         string | null
}

function fmtCountdown(ms: number): string {
  const total = Math.max(0, Math.floor(ms / 1000))
  return `${String(Math.floor(total / 60)).padStart(2, '0')}:${String(total % 60).padStart(2, '0')}`
}

// ── Page ──────────────────────────────────────────────────────────────────────
export default function NodeProductPage() {
  const [nodes,  setNodes]  = useState<NodeRow[]>([])
  const [blocks, setBlocks] = useState<MiningBlock[]>([])
  const [nowMs,  setNowMs]  = useState(Date.now())
  const [lightbox, setLightbox] = useState<string | null>(null)   // fullsize image popup

  const refresh = useCallback(async () => {
    const [n, b] = await Promise.all([
      supabase.from('public_node_directory').select('node_code,display_name,is_verified,is_online,total_tusd_earned,blocks_won,created_at,lat,lng'),
      supabase.from('public_mining_feed').select('*').order('block_number', { ascending: false }).limit(24),
    ])
    setNodes((n.data ?? []) as NodeRow[])
    setBlocks((b.data ?? []) as MiningBlock[])
  }, [])

  useEffect(() => {
    refresh()
    const t = setInterval(refresh, 30_000)
    return () => clearInterval(t)
  }, [refresh])

  useEffect(() => {
    const t = setInterval(() => setNowMs(Date.now()), 1000)
    return () => clearInterval(t)
  }, [])

  const onlineCount = nodes.filter(n => n.is_online).length
  const totalBlocks = nodes.reduce((a, n) => a + n.blocks_won, 0)
  const totalTusd   = nodes.reduce((a, n) => a + n.total_tusd_earned, 0)

  const pendingBlock     = blocks.find(b => b.mined_at == null) ?? null
  // Mined lane mirrors the network page: oldest → newest flowing left to
  // right, auto-scrolled so the newest sits next to the divider.
  const minedOldestFirst = blocks.filter(b => b.mined_at != null)
                                 .sort((a, b) => a.block_number - b.block_number)

  const nextBlockAt = useMemo<Date | null>(() => {
    const last = blocks.find(b => b.mined_at != null)
    if (last?.mined_at) return new Date(new Date(last.mined_at).getTime() + BLOCK_INTERVAL_MS)
    if (pendingBlock?.created_at) return new Date(new Date(pendingBlock.created_at).getTime() + BLOCK_INTERVAL_MS)
    return null
  }, [blocks, pendingBlock])
  const countdown = nextBlockAt ? fmtCountdown(Math.max(0, nextBlockAt.getTime() - nowMs)) : '--:--'

  return (
    <div style={s.root}>
      {/* Hidden-scrollbar rule for the draggable block lane (WebKit needs a
          real stylesheet — there's no inline ::-webkit-scrollbar). */}
      <style>{`.np-lane::-webkit-scrollbar{display:none}`}</style>

      {/* ── Header ── */}
      <SiteHeader />

      {/* ── Hero ── */}
      <section style={s.hero}>
        <div style={s.heroText}>
          <div style={s.kicker}>Real hardware · Unstable signal</div>
          <h1 style={s.heroTitle}>The desk terminal of the <span style={{ color: C.green }}>₸USD</span> economy</h1>
          <p style={s.heroSub}>
            A 4-inch touch display that sits on your desk, joins the TurboUSD network, and mines
            ₸USD every hour just for being online. In between blocks it&apos;s your market ticker,
            your clock and alarm, your NFT gallery and the most honest inflation monitor money can buy.
          </p>
          <div style={s.ctaRow}>
            <Link href="/" style={s.ctaPrimary}>Live network →</Link>
            <a href="/setup" style={s.ctaSecondary}>Flash NodeOS →</a>
          </div>
          <p style={{ fontSize: 11, color: C.muted, marginTop: 14 }}>
            Runs on the Seeed SenseCAP Indicator D1. Off-the-shelf hardware, no soldering,
            flashed from your browser in two minutes.
          </p>
        </div>
        <div style={s.heroImgWrap}>
          <DeviceHero />
        </div>
      </section>

      {/* ── Feature grid ── */}
      <section style={s.section}>
        <div style={s.kicker}>One device, many screens</div>
        <h2 style={s.h2}>Everything it does, out of the box</h2>
        <p style={s.sectionSub}>Swipe between screens on the touch panel. Every one of them is live, configurable from the web, and synced to your node.</p>
        <div style={s.featureGrid}>
          <Feature icon="⛏" title="Mines ₸USD" color={C.green}
            text="Every hour the network opens a block and one online node wins it. Keep your node on your desk, keep it online, stack ₸USD. Verified nodes appear on the public leaderboard and world map." />
          <Feature icon="📈" title="Live tickers" color={C.yellow}
            text="Track ₸USD and any tokens you pick, with logos, sparklines and expandable candlestick charts. One or two columns, fully configured from your phone." />
          <Feature icon="⏰" title="Clock & alarm" color={C.blue}
            text="A proper bedside clock: big time, date, weekday alarms with a real buzzer. The screen wakes up on its own when the alarm fires, even from sleep." />
          <Feature icon="💸" title="Inflation game" color={C.red}
            text="Watch $10,000 lose purchasing power in real time, at the current US debt-derived rate, down to the fourth decimal, tick by tick. Painfully honest. Switch to 1-100 year horizons when you want the long view." />
          <Feature icon="🏛" title="US debt clock" color={C.red}
            text="The total US national debt, live and climbing, with a chart of how it got there and per-second / per-minute / per-hour rates since any window you choose." />
          <Feature icon="🖼" title="NFT gallery" color={C.blue}
            text="Show your Ethereum NFTs and Bitcoin Ordinals in 1×1, 2×2 or 3×3 grids with floor prices. Point it at your wallet and it curates by floor, or pin exactly the pieces you want on display." />
        </div>
      </section>

      {/* ── Hardware ── */}
      <section style={s.section}>
        <div style={s.kicker}>Under the hood</div>
        <h2 style={s.h2}>Serious little machine</h2>
        <div style={s.specRow}>
          <Spec value="4&quot;" label="480×480 IPS touch" />
          <Spec value="ESP32-S3" label="dual-core + 8 MB PSRAM" />
          <Spec value="RP2040" label="co-processor & buzzer" />
          <Spec value="Wi-Fi" label="that's all it needs" />
        </div>
        {/* On mobile the device photos span the full width of the section. */}
        <style>{`
          @media (max-width: 640px) {
            .hw-col { width: 100%; flex-basis: 100%; }
            .hw-img { width: 100% !important; max-width: 100% !important; }
          }
        `}</style>
        <div style={s.hwSplit}>
          <div className="hw-col" style={{ display: 'flex', flexDirection: 'column', gap: 12, flexShrink: 0 }}>
            {/* eslint-disable-next-line @next/next/no-img-element */}
            <img src={DEVICE_IMG_BACK} alt="SenseCAP Indicator D1 back: button, Grove ports, USB-C"
              className="hw-img" style={s.hwImg} onClick={() => setLightbox(DEVICE_IMG_BACK)} />
            {/* eslint-disable-next-line @next/next/no-img-element */}
            <img src={DEVICE_IMG_ALT} alt="SenseCAP Indicator D1 edge: internal button, USB-C, microSD, antenna"
              className="hw-img" style={s.hwImg} onClick={() => setLightbox(DEVICE_IMG_ALT)} />
          </div>
          <div style={{ flex: 1, minWidth: 260 }}>
            <p style={s.p}>
              The Node runs on the Seeed SenseCAP Indicator D1: an ESP32-S3 driving the round-corner
              IPS panel, paired with an RP2040 that handles the buzzer and expansion ports. Optional
              Grove sensors (temperature &amp; humidity) plug straight in.
            </p>
            <p style={s.p}>
              No accounts, no subscriptions, no cloud lock-in. Buy the hardware anywhere, open the
              web flasher, and your node is registered and mining in minutes. All settings (tickers,
              NFTs, screens, alarm) are managed from a simple web page and sync to the device automatically.
            </p>
            <a href={SEEED_STORE_URL} target="_blank" rel="noreferrer" style={{ ...s.ctaPrimary, display: 'inline-block', marginTop: 4 }}>
              Get the hardware →
            </a>
          </div>
        </div>
      </section>

      {/* ── Open source / OS ── */}
      <section style={s.section}>
        <div style={s.kicker}>NodeOS</div>
        <h2 style={s.h2}>Built by TurboUSD. Open to everyone.</h2>
        <p style={s.p}>
          The entire operating system was developed from scratch by the TurboUSD team and released
          fully open-source: firmware for both chips, the mining backend and this very website.
          Read it, audit it, fork it, improve it. No blobs, no secrets.
        </p>
        <p style={s.p}>
          Everything lives at <a href={GITHUB_URL} target="_blank" rel="noreferrer" style={s.inlineLink}>github.com/turbousd/node</a>:
          firmware, backend and web in one repo, with CI-built images you can flash straight from the browser.
        </p>
      </section>

      {/* ── For other projects ── */}
      <section style={s.sectionAlt}>
        <div style={{ maxWidth: 800, margin: '0 auto', padding: '0 20px' }}>
          <div style={s.kicker}>Not just for us</div>
          <h2 style={s.h2}>Adopt it for your own project</h2>
          <p style={s.p}>
            NodeOS was designed so any community can make it theirs: swap the branding, the default
            tickers and the stats screens, keep the mining network or run your own. If your token,
            DAO or collection wants physical presence on people&apos;s desks, this is the shortest path
            to it: the hardware is off-the-shelf and the software is already written.
          </p>
          <p style={s.p}>
            Everything is MIT-style permissive. Keeping a small &quot;powered by TurboUSD NodeOS&quot;
            reference is appreciated, but it&apos;s yours to build with.
          </p>
          <a href={GITHUB_URL} target="_blank" rel="noreferrer" style={s.ctaSecondary}>Start from the source →</a>
        </div>
      </section>

      {/* ── Live network slice ── */}
      <section style={s.section}>
        <div style={s.kicker}>It&apos;s already running</div>
        <h2 style={s.h2}>The network, right now</h2>

        <div style={s.statsBar}>
          <StatPill label="Total nodes" value={nodes.length}         color={C.text}  />
          <StatPill label="Online now"  value={onlineCount}          color={C.green} />
          <StatPill label="Blocks won"  value={totalBlocks}          color={C.yellow} />
          <StatPill label="₸ distributed" value={`₸${totalTusd.toFixed(0)}`} color={C.green} />
        </div>

        {/* Blocks strip — same layout as the network page: mined lane flows on
            the left, dashed divider, pending block pinned on the right.
            Scrollbar hidden; the lane is click-drag scrollable (and swipes
            natively on touch). */}
        <BlocksStrip mined={minedOldestFirst} pending={pendingBlock} countdown={countdown} />

        <MiniMap nodes={nodes} />

        <div style={{ display: 'flex', justifyContent: 'center', marginTop: 18 }}>
          <Link href="/" style={s.ctaPrimary}>Explore the full network →</Link>
        </div>
      </section>

      {/* ── Final CTA ── */}
      <section style={{ ...s.section, textAlign: 'center', paddingBottom: 90 }}>
        <h2 style={{ ...s.h2, fontSize: 30 }}>Ready to put one on your desk?</h2>
        <div style={{ ...s.ctaRow, justifyContent: 'center', marginTop: 20 }}>
          <a href={SEEED_STORE_URL} target="_blank" rel="noreferrer" style={s.ctaPrimary}>Get the hardware →</a>
          <a href="/setup" style={s.ctaSecondary}>Flash NodeOS →</a>
        </div>
        <p style={{ fontSize: 11, color: C.muted, marginTop: 22 }}>
          ₸USD rewards are for fun, not financial advice. The only guaranteed yield is a very cool desk.
        </p>
      </section>

      {/* ── Fullsize image popup ── */}
      {lightbox && (
        <div style={s.lightbox} onClick={() => setLightbox(null)} role="dialog" aria-label="Image preview">
          {/* eslint-disable-next-line @next/next/no-img-element */}
          <img src={lightbox} alt="" style={{ maxWidth: '92vw', maxHeight: '86vh', borderRadius: 12, background: '#fff' }} />
          <button aria-label="Close" onClick={() => setLightbox(null)} style={s.lightboxClose}>✕</button>
        </div>
      )}
    </div>
  )
}


// ── Animated device hero ──────────────────────────────────────────────────────
// The real D1 photo with a live NodeOS mock composited onto the panel. The
// screens slide sideways on a timer, as if the device were swiping itself.
const HERO_SCREENS = 6

function DeviceHero() {
  const [scr, setScr] = useState(0)
  const [now, setNow] = useState<Date | null>(null)

  useEffect(() => {
    setNow(new Date())
    const t = setInterval(() => setNow(new Date()), 1000)
    return () => clearInterval(t)
  }, [])
  useEffect(() => {
    const t = setInterval(() => setScr(v => (v + 1) % HERO_SCREENS), 3600)
    return () => clearInterval(t)
  }, [])

  const hh = now ? String(now.getHours()).padStart(2, '0')   : '00'
  const mm = now ? String(now.getMinutes()).padStart(2, '0') : '00'
  const ss = now ? now.getSeconds() : 0
  const dateStr = now
    ? now.toLocaleDateString('en-GB', { weekday: 'short', day: 'numeric', month: 'short' }).toUpperCase()
    : ''
  // Live-ish figures so the mock visibly ticks like the real firmware.
  const debt   = 38_412_007_113_450 + (Math.floor(Date.now() / 1000) % 86400) * 32_000
  const game   = 10_000 - ((Date.now() / 1000) % 3600) * 0.000821

  const head = (
    <div style={h.head}>
      <span>{dateStr}</span>
      <span style={{ color: C.text }}>{hh}:{mm}</span>
      <span>23°C</span>
    </div>
  )
  const foot = <div style={h.foot}><span>TONY SOPRANFTO</span><span style={{ opacity: .6 }}>NETWORK: 1 NODE</span></div>

  const screens = [
    // 1 · Clock
    <div key="clock" style={h.scr}>
      {head}
      <div style={{ flex: 1, display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center' }}>
        <div style={{ fontSize: 54, fontWeight: 800, letterSpacing: 1, color: C.text, fontVariantNumeric: 'tabular-nums' }}>
          {hh}<span style={{ opacity: ss % 2 ? 1 : .25 }}>:</span>{mm}
        </div>
        <div style={{ fontSize: 11, color: C.muted, letterSpacing: 2, marginTop: 4 }}>{dateStr}</div>
        <div style={{ fontSize: 10, color: C.yellow, marginTop: 10 }}>⏰ 07:30 · MON–FRI</div>
      </div>
      {foot}
    </div>,
    // 2 · Tickers
    <div key="tick" style={h.scr}>
      {head}
      <div style={{ flex: 1, display: 'flex', flexDirection: 'column', gap: 5, justifyContent: 'center' }}>
        {[
          ['₸USD', '$0.0141', '+4.2%',  true],
          ['ETH',  '$4,812',  '+1.1%',  true],
          ['BTC',  '$118,4k', '-0.6%',  false],
        ].map(([sym, px, ch, up]) => (
          <div key={sym as string} style={h.tickRow}>
            <span style={{ ...h.tickLogo, background: sym === '₸USD' ? '#123c26' : '#1c2233' }}>{(sym as string)[0]}</span>
            <span style={{ fontWeight: 700, fontSize: 12, color: C.text, width: 44 }}>{sym}</span>
            <Spark up={up as boolean} />
            <span style={{ fontSize: 12, color: C.text, marginLeft: 'auto', fontVariantNumeric: 'tabular-nums' }}>{px}</span>
            <span style={{ fontSize: 10, color: up ? C.green : C.red, width: 38, textAlign: 'right' }}>{ch}</span>
          </div>
        ))}
      </div>
      {foot}
    </div>,
    // 3 · US debt
    <div key="debt" style={h.scr}>
      {head}
      <div style={{ flex: 1, display: 'flex', flexDirection: 'column', justifyContent: 'center' }}>
        <div style={{ fontSize: 11, color: C.muted, letterSpacing: 1.5 }}>US TOTAL DEBT</div>
        <div style={{ fontSize: 21, fontWeight: 800, color: C.red, fontVariantNumeric: 'tabular-nums', margin: '2px 0 8px' }}>
          ${debt.toLocaleString('en-US')}
        </div>
        <Chart color={C.red} up />
        <div style={{ display: 'flex', justifyContent: 'space-between', marginTop: 8, fontSize: 9, color: C.muted }}>
          <span>SINCE NODE ON <span style={{ color: C.red, fontWeight: 700 }}>+$342.51k</span></span>
          <span>RATE/SEC <span style={{ color: C.red, fontWeight: 700 }}>+$32,001</span></span>
        </div>
      </div>
      {foot}
    </div>,
    // 4 · Inflation game
    <div key="game" style={h.scr}>
      {head}
      <div style={{ flex: 1, display: 'flex', flexDirection: 'column', justifyContent: 'center' }}>
        <div style={{ fontSize: 11, color: C.muted, letterSpacing: 1.5 }}>INFLATION GAME · REAL TIME</div>
        <div style={{ fontSize: 24, fontWeight: 800, color: C.yellow, fontVariantNumeric: 'tabular-nums', margin: '2px 0 8px' }}>
          ${game.toFixed(4)}
        </div>
        <Chart color={C.yellow} up={false} />
        <div style={{ fontSize: 9, color: C.muted, marginTop: 8 }}>
          WHAT $10,000 IS STILL WORTH <span style={{ color: C.red, fontWeight: 700 }}>-{(10000 - game).toFixed(4)}</span>
        </div>
      </div>
      {foot}
    </div>,
    // 5 · NFT gallery
    <div key="nft" style={h.scr}>
      {head}
      <div style={{ flex: 1, display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 5, alignContent: 'center' }}>
        {[['#f68b1f', 'NodeMonke #9343', '0.09 ₿'], ['#1c2233', 'CryptoPunk #7804', '42.5 Ξ'],
          ['#123c26', 'Milady #1337', '2.10 Ξ'],   ['#2a1a33', 'Remilio #404', '0.88 Ξ']].map(([bg, nm, fl]) => (
          <div key={nm as string} style={{ borderRadius: 5, overflow: 'hidden', border: '1px solid #1c1c1c' }}>
            <div style={{ height: 42, background: bg as string, display: 'flex', alignItems: 'center', justifyContent: 'center', fontSize: 18 }}>◉</div>
            <div style={{ background: '#000', padding: '2px 4px', display: 'flex', justifyContent: 'space-between', fontSize: 7, color: '#d8d8dc' }}>
              <span style={{ overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{nm}</span>
              <span style={{ flexShrink: 0, marginLeft: 3 }}>{fl}</span>
            </div>
          </div>
        ))}
      </div>
      {foot}
    </div>,
    // 6 · Node network
    <div key="node" style={h.scr}>
      {head}
      <div style={{ flex: 1, display: 'flex', flexDirection: 'column', justifyContent: 'center', gap: 7 }}>
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'baseline' }}>
          <span style={{ fontSize: 13, fontWeight: 800, color: C.text }}>TURBOUSD NETWORK</span>
          <span style={{ fontSize: 9, color: C.green }}>● LIVE MINING</span>
        </div>
        <div style={{ display: 'flex', gap: 4 }}>
          {['#812', '#813', '#814'].map(n => (
            <div key={n} style={{ flex: 1, borderRadius: 4, background: '#0d2917', border: `1px solid ${C.green}44`, textAlign: 'center', padding: '5px 0' }}>
              <div style={{ fontSize: 8, color: '#d8ffe6' }}>{n}</div>
              <div style={{ fontSize: 12, fontWeight: 800, color: C.text }}>₸100</div>
            </div>
          ))}
          <div style={{ flex: 1, borderRadius: 4, background: '#2b1f06', border: `1px solid ${C.yellow}44`, textAlign: 'center', padding: '5px 0' }}>
            <div style={{ fontSize: 8, color: '#ffe9b8' }}>#815</div>
            <div style={{ fontSize: 12, fontWeight: 800, color: C.yellow, fontVariantNumeric: 'tabular-nums' }}>{59 - (now ? now.getMinutes() : 0)}m</div>
          </div>
        </div>
        <div style={{ fontSize: 9, color: C.muted }}>₸ REWARDS · <span style={{ color: C.green }}>Tony SopraNFTo ₸12.40</span></div>
      </div>
      {foot}
    </div>,
  ]

  return (
    <div style={{ position: 'relative', width: '100%', maxWidth: 400 }}>
      {/* eslint-disable-next-line @next/next/no-img-element */}
      <img src={DEVICE_IMG} alt="TurboUSD Node device" style={{ width: '100%', height: 'auto', display: 'block', borderRadius: 18 }} />
      <div style={{ position: 'absolute', ...SCREEN_BOX, background: '#000', borderRadius: '7%', overflow: 'hidden', boxShadow: 'inset 0 0 14px rgba(0,0,0,.9)' }}>
        <div style={{
          display: 'flex', height: '100%', width: `${HERO_SCREENS * 100}%`,
          transform: `translateX(-${(scr * 100) / HERO_SCREENS}%)`,
          transition: 'transform .55s cubic-bezier(.25,.7,.3,1)',
        }}>
          {screens.map(node => (
            <div key={(node as React.ReactElement).key} style={{ width: `${100 / HERO_SCREENS}%`, height: '100%' }}>{node}</div>
          ))}
        </div>
      </div>
    </div>
  )
}

// Tiny inline chart / sparkline SVGs for the hero mock.
function Chart({ color, up }: { color: string; up: boolean }) {
  const pts = up
    ? '0,34 12,32 24,33 36,28 48,26 60,27 72,20 84,16 96,17 108,10 120,6 132,2'
    : '0,2 12,5 24,4 36,10 48,13 60,12 72,19 84,24 96,23 108,29 120,31 132,35'
  return (
    <svg viewBox="0 0 132 36" style={{ width: '100%', height: 36 }} preserveAspectRatio="none">
      <polyline points={pts} fill="none" stroke={color} strokeWidth="1.6" />
    </svg>
  )
}

function Spark({ up }: { up: boolean }) {
  const pts = up ? '0,10 8,8 16,9 24,5 32,6 40,2' : '0,2 8,4 16,3 24,7 32,6 40,10'
  return (
    <svg viewBox="0 0 40 12" style={{ width: 40, height: 12, flexShrink: 0 }} preserveAspectRatio="none">
      <polyline points={pts} fill="none" stroke={up ? C.green : C.red} strokeWidth="1.4" />
    </svg>
  )
}

// ── Blocks strip (network-page layout + drag-to-scroll, hidden scrollbar) ─────
function BlocksStrip({ mined, pending, countdown }: {
  mined: MiningBlock[]; pending: MiningBlock | null; countdown: string
}) {
  const laneRef = useRef<HTMLDivElement>(null)
  const drag    = useRef({ on: false, startX: 0, startScroll: 0 })

  // Newest mined block parks next to the divider (auto-scroll right once per
  // new block, exactly like the network page).
  useEffect(() => {
    const el = laneRef.current
    if (el && el.dataset.autoscrolled !== String(mined.length)) {
      el.scrollLeft = el.scrollWidth
      el.dataset.autoscrolled = String(mined.length)
    }
  }, [mined.length])

  // Click-drag to scroll on desktop (touch scrolls natively).
  function onDown(e: React.MouseEvent) {
    const el = laneRef.current
    if (!el) return
    drag.current = { on: true, startX: e.clientX, startScroll: el.scrollLeft }
    const move = (ev: MouseEvent) => {
      if (!drag.current.on || !laneRef.current) return
      laneRef.current.scrollLeft = drag.current.startScroll - (ev.clientX - drag.current.startX)
      ev.preventDefault()
    }
    const upH = () => {
      drag.current.on = false
      window.removeEventListener('mousemove', move)
      window.removeEventListener('mouseup', upH)
    }
    window.addEventListener('mousemove', move)
    window.addEventListener('mouseup', upH)
    e.preventDefault()
  }

  return (
    <div style={s.blockStrip}>
      <div ref={laneRef} className="np-lane" style={s.blockLane} onMouseDown={onDown}>
        {mined.length === 0 && (
          <span style={{ alignSelf: 'center', color: C.muted, fontSize: 11, whiteSpace: 'nowrap' }}>
            No blocks mined yet
          </span>
        )}
        {mined.map(b => (
          <div key={b.block_number} style={{ ...s.blockTile, ...s.blockMined }}>
            <div style={s.blockNum}>#{b.block_number}</div>
            <div style={{ fontSize: 14, fontWeight: 'bold', color: C.green }}>₸{b.reward_tusd}</div>
            <div style={{ fontSize: 10, color: C.text, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap', maxWidth: 84 }}>
              {b.winner_display_name ?? '—'}
            </div>
          </div>
        ))}
      </div>
      <div style={s.blockDivider} />
      {pending && (
        <div style={{ ...s.blockTile, ...s.blockPending, marginRight: 2 }}>
          <div style={s.blockNum}>#{pending.block_number}</div>
          <div style={{ fontSize: 15, fontWeight: 'bold', color: C.yellow, fontVariantNumeric: 'tabular-nums' }}>{countdown}</div>
          <div style={{ fontSize: 10, color: C.muted }}>mining…</div>
        </div>
      )}
    </div>
  )
}

// ── Small components ──────────────────────────────────────────────────────────
function Feature({ icon, title, text, color }: { icon: string; title: string; text: string; color: string }) {
  return (
    <div style={s.featureCard}>
      <div style={{ fontSize: 26, lineHeight: 1 }}>{icon}</div>
      <div style={{ fontSize: 15, fontWeight: 700, color, margin: '10px 0 6px' }}>{title}</div>
      <div style={{ fontSize: 13, color: '#a8adb8', lineHeight: 1.55 }}>{text}</div>
    </div>
  )
}

function Spec({ value, label }: { value: string; label: string }) {
  return (
    <div style={s.specPill}>
      <div style={{ fontSize: 18, fontWeight: 'bold', color: C.text }} dangerouslySetInnerHTML={{ __html: value }} />
      <div style={{ fontSize: 10, color: C.muted, marginTop: 3, textTransform: 'uppercase', letterSpacing: 0.6 }}>{label}</div>
    </div>
  )
}

function StatPill({ label, value, color }: { label: string; value: number | string; color: string }) {
  return (
    <div style={s.statPill}>
      <div style={{ fontSize: 20, fontWeight: 'bold', color }}>{value}</div>
      <div style={{ fontSize: 10, color: C.muted, marginTop: 2, textTransform: 'uppercase', letterSpacing: 0.8 }}>{label}</div>
    </div>
  )
}

// Compact, read-only version of the network map: markers only, no popups —
// clicking anywhere sends you to the full network page.
function MiniMap({ nodes }: { nodes: NodeRow[] }) {
  const containerRef = useRef<HTMLDivElement>(null)
  const mapRef       = useRef<any>(null)

  const geoNodes = useMemo(() => nodes.filter(n => n.lat != null && n.lng != null), [nodes])

  useEffect(() => {
    if (!containerRef.current || geoNodes.length === 0) return

    function buildMarkers(L: any) {
      if (!mapRef.current) return
      mapRef.current.eachLayer((layer: any) => {
        if (layer._isNodeMarker) mapRef.current.removeLayer(layer)
      })
      geoNodes.forEach(node => {
        const online = node.is_online
        const marker = L.circleMarker([node.lat, node.lng], {
          radius:      online ? 6 : 4,
          color:       online ? C.green : '#444',
          fillColor:   online ? C.green : '#333',
          fillOpacity: online ? 0.9 : 0.55,
          weight:      online ? 2 : 1,
          interactive: false,
        })
        marker._isNodeMarker = true
        marker.addTo(mapRef.current)
      })
    }

    function initMap() {
      const L = (window as any).L
      if (!L || !containerRef.current) return
      if (mapRef.current) { buildMarkers(L); return }
      const map = L.map(containerRef.current, {
        center: [25, 10], zoom: 1, minZoom: 1,
        scrollWheelZoom: false, dragging: false, zoomControl: false,
        doubleClickZoom: false, boxZoom: false, keyboard: false,
        attributionControl: false, worldCopyJump: true,
      })
      L.tileLayer(
        'https://server.arcgisonline.com/ArcGIS/rest/services/Canvas/World_Dark_Gray_Base/MapServer/tile/{z}/{y}/{x}',
        { maxZoom: 16 }
      ).addTo(map)
      mapRef.current = map
      buildMarkers(L)
    }

    if ((window as any).L) {
      initMap()
    } else {
      if (!document.getElementById('leaflet-css')) {
        const link = document.createElement('link')
        link.id = 'leaflet-css'; link.rel = 'stylesheet'
        link.href = 'https://unpkg.com/leaflet@1.9.4/dist/leaflet.css'
        document.head.appendChild(link)
      }
      if (!document.getElementById('leaflet-js')) {
        const script = document.createElement('script')
        script.id = 'leaflet-js'
        script.src = 'https://unpkg.com/leaflet@1.9.4/dist/leaflet.js'
        script.onload = initMap
        document.head.appendChild(script)
      } else {
        // Script tag exists (injected by another page) but may still be loading
        const t = setInterval(() => { if ((window as any).L) { clearInterval(t); initMap() } }, 200)
        return () => clearInterval(t)
      }
    }

    return () => {
      if (mapRef.current) { mapRef.current.remove(); mapRef.current = null }
    }
  }, [geoNodes])

  if (geoNodes.length === 0) return null

  return (
    <Link href="/" style={{ display: 'block', textDecoration: 'none' }} aria-label="Open the network map">
      <div style={s.miniMapWrap}>
        <div ref={containerRef} style={{ width: '100%', height: '100%', background: '#0a0a0a' }} />
        <div style={s.miniMapOverlay} />
      </div>
    </Link>
  )
}

// ── Hero-mock styles ──────────────────────────────────────────────────────────
const h: Record<string, React.CSSProperties> = {
  scr: {
    width: '100%', height: '100%', background: '#000', color: C.text,
    display: 'flex', flexDirection: 'column', padding: '7% 8%',
    fontFamily: 'system-ui, -apple-system, sans-serif',
  },
  head: { display: 'flex', justifyContent: 'space-between', fontSize: 9, color: C.muted, letterSpacing: 1 },
  foot: { display: 'flex', justifyContent: 'space-between', fontSize: 8, color: C.muted, letterSpacing: 1 },
  tickRow: {
    display: 'flex', alignItems: 'center', gap: 6,
    background: '#0c0c0c', border: '1px solid #1c1c1c', borderRadius: 6, padding: '5px 7px',
  },
  tickLogo: {
    width: 18, height: 18, borderRadius: '50%', flexShrink: 0,
    display: 'flex', alignItems: 'center', justifyContent: 'center',
    fontSize: 9, fontWeight: 700, color: C.text,
  },
}

// ── Styles ────────────────────────────────────────────────────────────────────
const s: Record<string, React.CSSProperties> = {
  root: { minHeight: '100vh', background: C.bg, color: C.text, fontFamily: 'system-ui, -apple-system, sans-serif' },

  header: {
    borderBottom: `1px solid ${C.border}`, position: 'sticky', top: 0, zIndex: 100,
    background: 'rgba(0,0,0,0.92)', backdropFilter: 'blur(12px)',
  },
  headerInner: {
    maxWidth: 1100, margin: '0 auto', padding: '0 20px', height: 56,
    display: 'flex', alignItems: 'center', justifyContent: 'space-between',
  },
  logo:     { fontSize: 18, fontWeight: 'bold', letterSpacing: -0.5 },
  navLink:  { color: C.muted, fontSize: 13, fontWeight: 600, textDecoration: 'none', whiteSpace: 'nowrap' },
  setupBtn: { padding: '7px 18px', background: C.green, color: C.onGreen, borderRadius: 20, fontWeight: 'bold', fontSize: 13, textDecoration: 'none', whiteSpace: 'nowrap' },

  hero: {
    maxWidth: 1000, margin: '0 auto', padding: '64px 20px 30px',
    display: 'flex', alignItems: 'center', gap: 36, flexWrap: 'wrap',
  },
  heroText:  { flex: '1 1 380px', minWidth: 300 },
  kicker:    { fontSize: 11, fontWeight: 700, color: C.green, textTransform: 'uppercase', letterSpacing: 2, marginBottom: 12 },
  heroTitle: { fontSize: 40, fontWeight: 800, lineHeight: 1.12, letterSpacing: -1, margin: 0 },
  heroSub:   { fontSize: 15, color: '#a8adb8', lineHeight: 1.6, marginTop: 18, maxWidth: 480 },
  ctaRow:    { display: 'flex', gap: 10, marginTop: 26, flexWrap: 'wrap' },
  ctaPrimary: {
    padding: '11px 22px', background: C.green, color: C.onGreen, borderRadius: 24,
    fontWeight: 'bold', fontSize: 14, textDecoration: 'none', whiteSpace: 'nowrap',
  },
  ctaSecondary: {
    padding: '11px 22px', background: 'transparent', color: C.text, borderRadius: 24,
    border: `1px solid #2c2c2c`, fontWeight: 600, fontSize: 14, textDecoration: 'none', whiteSpace: 'nowrap',
  },
  heroImgWrap: { flex: '1 1 320px', minWidth: 280, display: 'flex', justifyContent: 'center' },

  section:    { maxWidth: 800, margin: '0 auto', padding: '56px 20px 8px' },
  sectionAlt: { background: '#070a08', borderTop: `1px solid ${C.border}`, borderBottom: `1px solid ${C.border}`, padding: '56px 0 48px', marginTop: 56 },
  h2:         { fontSize: 26, fontWeight: 800, letterSpacing: -0.5, margin: '0 0 8px' },
  sectionSub: { fontSize: 14, color: C.muted, margin: '0 0 26px', lineHeight: 1.6 },
  p:          { fontSize: 14, color: '#a8adb8', lineHeight: 1.7, margin: '0 0 16px', maxWidth: 640 },
  inlineLink: { color: C.green, textDecoration: 'none', fontWeight: 600 },

  featureGrid: {
    display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(230px, 1fr))', gap: 12,
  },
  featureCard: {
    background: C.card, border: `1px solid ${C.border}`, borderRadius: 14, padding: '18px 18px 20px',
  },

  specRow: { display: 'flex', gap: 10, flexWrap: 'wrap', margin: '18px 0 28px' },
  specPill: {
    background: C.card, border: `1px solid ${C.border}`, borderRadius: 12,
    padding: '12px 18px', flex: '1 1 130px', textAlign: 'center', minWidth: 120,
  },
  hwSplit: { display: 'flex', gap: 28, alignItems: 'center', flexWrap: 'wrap' },
  hwImg:   { width: 260, maxWidth: '100%', height: 'auto', borderRadius: 16, flexShrink: 0, cursor: 'zoom-in', background: '#fff' },

  lightbox: {
    position: 'fixed', inset: 0, zIndex: 3000, background: 'rgba(0,0,0,.85)',
    display: 'flex', alignItems: 'center', justifyContent: 'center', padding: 24, cursor: 'zoom-out',
  },
  lightboxClose: {
    position: 'absolute', top: 18, right: 22, width: 36, height: 36, borderRadius: '50%',
    background: C.surface, border: `1px solid ${C.border}`, color: C.text,
    fontSize: 15, cursor: 'pointer',
  },

  statsBar: { display: 'flex', gap: 8, flexWrap: 'wrap', margin: '18px 0 16px' },
  statPill: {
    background: C.card, border: `1px solid ${C.border}`, borderRadius: 12,
    padding: '12px 8px', textAlign: 'center', flex: '1 1 0', minWidth: 110,
  },

  // Blocks strip: mined lane | dashed divider | pending (fixed right)
  blockStrip: { display: 'flex', alignItems: 'stretch', padding: '4px 0 6px' },
  blockLane: {
    display: 'flex', gap: 8, overflowX: 'auto', minWidth: 0, flex: 1,
    cursor: 'grab', scrollbarWidth: 'none' as const, msOverflowStyle: 'none' as const,
    paddingBottom: 2, userSelect: 'none' as const,
  },
  blockDivider: { width: 0, borderLeft: '2px dashed #e8e8e8', margin: '4px 14px', opacity: 0.7, flexShrink: 0 },
  blockTile: {
    minWidth: 92, padding: '10px 10px 9px', borderRadius: 8, textAlign: 'center', flexShrink: 0,
    display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 3, justifyContent: 'center',
  },
  blockMined:   { background: 'linear-gradient(160deg,#081a10,#050d08)', border: `1px solid ${C.green}28` },
  blockPending: { background: 'linear-gradient(160deg,#1a1300,#0a0800)', border: `1px solid ${C.yellow}28` },
  blockNum:     { fontSize: 11, fontWeight: 700, color: '#c4c4cc', letterSpacing: 0.5 },

  miniMapWrap: {
    position: 'relative', height: 240, borderRadius: 14, overflow: 'hidden',
    border: `1px solid ${C.border}`, marginTop: 6, cursor: 'pointer',
  },
  // Transparent overlay: swallows map interactions so the whole thing acts as a link
  miniMapOverlay: { position: 'absolute', inset: 0, zIndex: 500 },
}

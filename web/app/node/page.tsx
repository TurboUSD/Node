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
  card:    '#121214',   // was #0c0c0c — cards were nearly invisible on black
  surface: '#1b1b1e',   // was #141414
  border:  '#2a2a2e',   // was #1c1c1c
  text:    '#e8e8e8',
  muted:   '#9096a1',   // was #6e7280 — secondary text was too dark
}

const BLOCK_INTERVAL_MS = 60 * 60 * 1000
const SEEED_STORE_URL   = 'https://www.seeedstudio.com/SenseCAP-Indicator-D1-p-5643.html'
const GITHUB_URL        = 'https://github.com/turbousd/node'
// Hardware section: same two annotated views the README shows, back (ports)
// on top, edge (button/USB/microSD/antenna) below. Click → fullsize popup.
const DEVICE_IMG_BACK = 'https://files.seeedstudio.com/wiki/SenseCAP/SenseCAP_Indicator/SenseCAP_Indicator_2.png'
const DEVICE_IMG_ALT  = 'https://files.seeedstudio.com/wiki/SenseCAP/SenseCAP_Indicator/SenseCAP_Indicator_3.png'

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
  winner_country?:     string | null
  // Winner's favourite community (from node_projects), shown where the winner
  // name used to sit on mined tiles.
  winner_project_name?:   string | null
  winner_project_symbol?: string | null
  winner_project_count?:  number | null   // total communities the winner has; extras = count - 1
  mined_at:            string | null
  created_at?:         string | null
}

function timeSince(iso: string): string {
  const sec = Math.floor((Date.now() - new Date(iso).getTime()) / 1000)
  if (sec < 60)    return `${sec}s ago`
  if (sec < 3600)  return `${Math.floor(sec / 60)}m ago`
  if (sec < 86400) return `${Math.floor(sec / 3600)}h ago`
  const d = Math.floor(sec / 86400)
  return d === 1 ? '1 day ago' : `${d} days ago`
}

// Winner's favourite community label for block tiles: the favourite's
// symbol/name, plus "(+N)" when the winner belongs to N more communities.
function projectLabel(symbol?: string | null, name?: string | null, count?: number | null): string {
  const base = symbol || name
  if (!base) return '—'
  const extra = (count ?? 0) - 1
  return extra > 0 ? `${base} (+${extra})` : base
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
    // Prefer the pending block's own open time so a backend countdown-restart
    // (unmined block, no node online) shows up here instead of freezing at 0.
    if (pendingBlock?.created_at) return new Date(new Date(pendingBlock.created_at).getTime() + BLOCK_INTERVAL_MS)
    const last = blocks.find(b => b.mined_at != null)
    if (last?.mined_at) return new Date(new Date(last.mined_at).getTime() + BLOCK_INTERVAL_MS)
    return null
  }, [blocks, pendingBlock])
  const msLeft    = nextBlockAt ? Math.max(0, nextBlockAt.getTime() - nowMs) : 0
  const circlePct = nextBlockAt ? msLeft / BLOCK_INTERVAL_MS : 0   // 1→0
  const minsLeft  = Math.ceil(msLeft / 60_000)

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
          <DeviceRenderCarousel />
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
            text="Track ₸USD and any tokens you pick, with logos, sparklines and expandable candlestick charts. Set market-cap alerts that ring the device like an alarm when a token crosses your line." />
          <Feature icon="⏰" title="Clock & alarm" color={C.blue}
            text="A proper bedside clock: big time, date, weekday alarms with a real buzzer. The screen wakes up on its own when the alarm fires, even from sleep." />
          <Feature icon="🖼" title="NFT gallery" color={C.blue}
            text="Your Ethereum NFTs and Bitcoin Ordinals in 1×1, 2×2 or 3×3 grids with live floors, floor-price alerts and a fullscreen photo-frame mode. Point it at your wallet or pin exactly what you want on display." />
          <Feature icon="💸" title="Inflation game" color={C.red}
            text="Watch $10,000 lose purchasing power in real time, at the current US debt-derived rate, down to the fourth decimal, tick by tick. Painfully honest. Switch to 1-100 year horizons when you want the long view." />
          <Feature icon="🏛" title="US debt clock" color={C.red}
            text="The total US national debt, live and climbing, with a chart of how it got there and per-second / per-minute / per-hour rates since any window you choose." />
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
          <StatPill label="Blocks mined" value={totalBlocks}         color={C.yellow} />
          <StatPill label="₸ distributed" value={`₸${totalTusd.toFixed(0)}`} color={C.green} />
        </div>

        {/* Blocks strip — same layout as the network page: mined lane flows on
            the left, dashed divider, pending block pinned on the right.
            Scrollbar hidden; the lane is click-drag scrollable (and swipes
            natively on touch). */}
        <BlocksStrip mined={minedOldestFirst} pending={pendingBlock} circlePct={circlePct} minsLeft={minsLeft} />

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
// ── Device hero: auto-swiping carousel of the real device renders ─────────────
// Seven render PNGs (web/public/device-renders/1..7.webp), slid sideways on a
// timer so the hero looks like the device swiping through its own screens.
const DEVICE_RENDER_COUNT = 7
const DEVICE_RENDER_MS    = 2600

// Approx. rectangle of the device SCREEN within each render (all 7 renders share
// the same device pose, so a fixed box works). Grab & drag L/R inside it to
// change the screen — feels like swiping the real touch panel.
const SCREEN_ZONE = { left: '27%', top: '15%', width: '53%', height: '58%' }

function DeviceRenderCarousel() {
  const [idx, setIdx] = useState(0)
  const N = DEVICE_RENDER_COUNT
  const autoRef = useRef<ReturnType<typeof setInterval> | null>(null)
  const dragRef = useRef<{ x: number; active: boolean }>({ x: 0, active: false })

  const stopAuto  = () => { if (autoRef.current) { clearInterval(autoRef.current); autoRef.current = null } }
  const startAuto = () => { stopAuto(); autoRef.current = setInterval(() => setIdx(i => (i + 1) % N), DEVICE_RENDER_MS) }

  useEffect(() => { startAuto(); return stopAuto }, []) // eslint-disable-line react-hooks/exhaustive-deps

  const go = (dir: number) => setIdx(i => (i + dir + N) % N)

  const onDown = (e: React.PointerEvent) => {
    dragRef.current = { x: e.clientX, active: true }
    stopAuto()
    ;(e.currentTarget as HTMLElement).setPointerCapture?.(e.pointerId)
  }
  const onUp = (e: React.PointerEvent) => {
    if (!dragRef.current.active) return
    const dx = e.clientX - dragRef.current.x
    dragRef.current.active = false
    if (dx <= -28) go(1)          // swipe left  → next screen
    else if (dx >= 28) go(-1)     // swipe right → previous screen
    startAuto()
  }

  return (
    <div style={{ width: '100%', maxWidth: 420 }}>
      {/* The DEVICE stays put; only the SCREEN changes — the 7 renders are
          identical except for the panel, so we crossfade the whole image and
          only the screen visibly changes. */}
      <div style={{ position: 'relative', width: '100%', aspectRatio: '1000 / 981' }}>
        {Array.from({ length: N }).map((_, i) => (
          // eslint-disable-next-line @next/next/no-img-element
          <img key={i} src={`/device-renders/${i + 1}.webp`} alt="TurboUSD Node screen"
            loading={i === 0 ? 'eager' : 'lazy'} draggable={false}
            style={{
              position: 'absolute', inset: 0, width: '100%', height: '100%',
              objectFit: 'contain', borderRadius: 18,
              opacity: i === idx ? 1 : 0, transition: 'opacity 420ms ease',
              userSelect: 'none', pointerEvents: 'none',
            }} />
        ))}
        {/* Swipe zone over the screen (works with mouse AND touch via Pointer
            Events; touch-action:none stops the page from scrolling mid-swipe). */}
        <div
          onPointerDown={onDown}
          onPointerUp={onUp}
          onPointerCancel={() => { dragRef.current.active = false; startAuto() }}
          style={{
            position: 'absolute', ...SCREEN_ZONE,
            cursor: 'grab', touchAction: 'none', borderRadius: '5%',
          }}
        />
      </div>
      {/* Position dots */}
      <div style={{ display: 'flex', gap: 6, justifyContent: 'center', marginTop: 14 }}>
        {Array.from({ length: N }).map((_, i) => (
          <button key={i} aria-label={`Screen ${i + 1}`}
            onClick={() => { stopAuto(); setIdx(i); startAuto() }}
            style={{
              width: 7, height: 7, borderRadius: '50%', border: 'none', padding: 0, cursor: 'pointer',
              background: i === idx ? C.green : '#3a3a3a', transition: 'background 250ms ease',
            }} />
        ))}
      </div>
    </div>
  )
}

// ── Blocks strip (network-page layout + drag-to-scroll, hidden scrollbar) ─────
const CIRC = 2 * Math.PI * 20  // r=20 → circumference ≈ 125.66

function BlocksStrip({ mined, pending, circlePct, minsLeft }: {
  mined: MiningBlock[]; pending: MiningBlock | null; circlePct: number; minsLeft: number
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
            <div style={s.blockAgo}>{b.mined_at ? timeSince(b.mined_at) : ''}</div>
            {/* Winner name — prominent, where the reward used to be */}
            <div style={s.blockWinnerBig}>{b.winner_display_name ?? '—'}</div>
            {/* Winner's favourite community + "(+N)" extra communities they added */}
            <div style={s.blockWinner}>{projectLabel(b.winner_project_symbol, b.winner_project_name, b.winner_project_count)}</div>
            <div style={s.blockCountry}>{b.winner_country || ' '}</div>
          </div>
        ))}
      </div>
      <div style={s.blockDivider} />
      {pending && (
        <div style={{ ...s.blockTile, ...s.blockPending }}>
          <div style={s.blockNum}>#{pending.block_number}</div>
          <div style={s.blockAgo}>mining…</div>
          <svg width="44" height="44" viewBox="0 0 52 52">
            <circle cx="26" cy="26" r="20" fill="none" stroke={`${C.yellow}22`} strokeWidth="3" />
            <circle cx="26" cy="26" r="20" fill="none" stroke={C.yellow} strokeWidth="3" strokeLinecap="round"
              strokeDasharray={CIRC} strokeDashoffset={CIRC * (1 - Math.max(0, Math.min(1, circlePct)))}
              transform="rotate(-90 26 26)" />
            <text x="26" y="26" textAnchor="middle" dominantBaseline="central"
              fill={C.yellow} fontSize="15" fontWeight="bold" fontFamily="system-ui, sans-serif">{minsLeft}</text>
          </svg>
          <div style={s.blockCountry}>Pending miner</div>
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

  // Blocks strip: mined lane | dashed divider | pending. Matches the network
  // page layout (same tile height + info) so both read identically.
  blockStrip: { display: 'flex', alignItems: 'stretch', padding: '4px 0 14px' },   // bottom pad = gap to the map
  blockLane: {
    display: 'flex', gap: 14, overflowX: 'auto', minWidth: 0, flex: 1,
    cursor: 'grab', scrollbarWidth: 'none' as const, msOverflowStyle: 'none' as const,
    paddingBottom: 16, userSelect: 'none' as const,   // room for the 3D shadow
  },
  blockDivider: { width: 0, borderLeft: '2px dashed #e8e8e8', margin: '4px 14px 20px', opacity: 0.7, flexShrink: 0 },
  blockTile: {
    minWidth: 100, height: 104, padding: '8px 9px', borderRadius: 8, textAlign: 'center', flexShrink: 0,
    display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'space-between',
    marginBottom: 8,   // extra room below for the shadow
  },
  // Single-tone 3D extrusion to the lower-LEFT (matches the network page).
  blockMined:   { background: '#1b4d2e', border: `1px solid ${C.green}66`,
                  boxShadow: '-3px 4px 0 #143f26, -6px 8px 0 #143f26, -8px 12px 0 #143f26, -9px 15px 13px rgba(0,0,0,0.5)' },
  blockPending: { background: '#4d3c15', border: `1px solid ${C.yellow}66`, marginLeft: 6,
                  boxShadow: '-3px 4px 0 #3a2c0f, -6px 8px 0 #3a2c0f, -8px 12px 0 #3a2c0f, -9px 15px 13px rgba(0,0,0,0.5)' },
  blockNum:     { fontSize: 12, fontWeight: 700, color: '#e8e8ea', letterSpacing: 0.5 },
  blockAgo:     { fontSize: 9,  color: '#a4a8b2' },
  blockReward:  { fontSize: 16, fontWeight: 'bold', color: C.green },
  // Winner name — prominent, sits where the reward used to be on mined tiles
  blockWinnerBig: { fontSize: 10.5, fontWeight: 'bold', color: C.green, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap', maxWidth: 96 },
  // Winner's favourite community — sits where the winner name used to be
  blockWinner:  { fontSize: 11, fontWeight: 600, color: '#e8e8e8', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap', maxWidth: 92 },
  blockCountry: { fontSize: 9, color: '#a4a8b2', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap', maxWidth: 92 },

  miniMapWrap: {
    // Full 240 on desktop; ~25% shorter on phones (48vw ≈ 180 on a ~390px
    // screen) so the map doesn't dominate the mobile layout.
    position: 'relative', height: 'clamp(170px, 48vw, 240px)', borderRadius: 14, overflow: 'hidden',
    border: `1px solid ${C.border}`, marginTop: 6, cursor: 'pointer',
  },
  // Transparent overlay: swallows map interactions so the whole thing acts as a link
  miniMapOverlay: { position: 'absolute', inset: 0, zIndex: 500 },
}

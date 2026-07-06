'use client'

// app/node/page.tsx — network.turbousd.com/node
//
// Product page for the TurboUSD Node device: what it is, what it does, why
// you want one on your desk. Ends with a live slice of the network (stats,
// block ticker, compact map) that links back to the main network page.

import { useEffect, useState, useMemo, useRef, useCallback } from 'react'
import Link from 'next/link'
import { supabase } from '@/lib/supabase'

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
const DEVICE_IMG        = 'https://files.seeedstudio.com/wiki/SenseCAP/SenseCAP_Indicator/SenseCAP_Indicator_2.png'
const DEVICE_IMG_ALT    = 'https://files.seeedstudio.com/wiki/SenseCAP/SenseCAP_Indicator/SenseCAP_Indicator_3.png'

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
  const [savedNodeCode, setSavedNodeCode] = useState<string | null>(null)

  useEffect(() => {
    const code = localStorage.getItem('turbousd_node_code')
    if (code) setSavedNodeCode(code)
  }, [])

  const refresh = useCallback(async () => {
    const [n, b] = await Promise.all([
      supabase.from('public_node_directory').select('node_code,display_name,is_verified,is_online,total_tusd_earned,blocks_won,created_at,lat,lng'),
      supabase.from('public_mining_feed').select('*').order('block_number', { ascending: false }).limit(12),
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
  const minedNewestFirst = blocks.filter(b => b.mined_at != null)

  const nextBlockAt = useMemo<Date | null>(() => {
    const last = blocks.find(b => b.mined_at != null)
    if (last?.mined_at) return new Date(new Date(last.mined_at).getTime() + BLOCK_INTERVAL_MS)
    if (pendingBlock?.created_at) return new Date(new Date(pendingBlock.created_at).getTime() + BLOCK_INTERVAL_MS)
    return null
  }, [blocks, pendingBlock])
  const countdown = nextBlockAt ? fmtCountdown(Math.max(0, nextBlockAt.getTime() - nowMs)) : '--:--'

  const setupHref = savedNodeCode ? `/setup/${savedNodeCode}` : '/setup'

  return (
    <div style={s.root}>

      {/* ── Header ── */}
      <header style={s.header}>
        <div style={s.headerInner}>
          <Link href="/" style={{ display: 'flex', alignItems: 'center', gap: 10, textDecoration: 'none', color: C.text }}>
            {/* eslint-disable-next-line @next/next/no-img-element */}
            <img
              src="https://turbousd.com/wp-content/uploads/2025/07/TurboUSD_t.png"
              alt="₸USD" style={{ height: 36, width: 'auto', objectFit: 'contain', display: 'block' }}
            />
            <span style={s.logo}>₸USD Node</span>
          </Link>
          <div style={{ display: 'flex', alignItems: 'center', gap: 18 }}>
            <Link href="/" style={s.navLink}>Network</Link>
            <a href={setupHref} style={s.setupBtn}>{savedNodeCode ? 'My Node →' : 'Setup →'}</a>
          </div>
        </div>
      </header>

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
            <a href={SEEED_STORE_URL} target="_blank" rel="noreferrer" style={s.ctaPrimary}>Get the hardware →</a>
            <a href={setupHref} style={s.ctaSecondary}>Flash NodeOS →</a>
            <Link href="/" style={s.ctaSecondary}>Live network →</Link>
          </div>
          <p style={{ fontSize: 11, color: C.muted, marginTop: 14 }}>
            Runs on the Seeed SenseCAP Indicator D1 — off-the-shelf hardware, no soldering.
            Flash it from your browser in two minutes.
          </p>
        </div>
        <div style={s.heroImgWrap}>
          {/* eslint-disable-next-line @next/next/no-img-element */}
          <img src={DEVICE_IMG} alt="TurboUSD Node device" style={s.heroImg} />
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
            text="A proper bedside clock: big time, date, weekday alarms with a real buzzer. The screen wakes up on its own when the alarm fires — even from sleep." />
          <Feature icon="💸" title="Inflation game" color={C.red}
            text="Watch $10,000 lose purchasing power in real time, at the current US debt-derived rate — down to the fourth decimal, tick by tick. Painfully honest. Switch to 1–100 year horizons when you want the long view." />
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
        <div style={s.hwSplit}>
          {/* eslint-disable-next-line @next/next/no-img-element */}
          <img src={DEVICE_IMG_ALT} alt="TurboUSD Node hardware" style={s.hwImg} />
          <div style={{ flex: 1, minWidth: 260 }}>
            <p style={s.p}>
              The Node runs on the Seeed SenseCAP Indicator D1: an ESP32-S3 driving the round-corner
              IPS panel, paired with an RP2040 that handles the buzzer and expansion ports. Optional
              Grove sensors (temperature &amp; humidity) plug straight in.
            </p>
            <p style={s.p}>
              No accounts, no subscriptions, no cloud lock-in. Buy the hardware anywhere, open the
              web flasher, and your node is registered and mining in minutes. All settings — tickers,
              NFTs, screens, alarm — are managed from a simple web page and sync to the device automatically.
            </p>
            <a href={SEEED_STORE_URL} target="_blank" rel="noreferrer" style={{ ...s.ctaSecondary, display: 'inline-block', marginTop: 4 }}>
              SenseCAP Indicator D1 on Seeed Studio →
            </a>
          </div>
        </div>
      </section>

      {/* ── Open source / OS ── */}
      <section style={s.section}>
        <div style={s.kicker}>NodeOS</div>
        <h2 style={s.h2}>Built by TurboUSD. Open to everyone.</h2>
        <p style={s.p}>
          The entire operating system — firmware for both chips, the mining backend and this very
          website — was developed from scratch by the TurboUSD team and released fully open-source.
          Read it, audit it, fork it, improve it. No blobs, no secrets.
        </p>
        <p style={s.p}>
          <a href={GITHUB_URL} target="_blank" rel="noreferrer" style={s.inlineLink}>github.com/turbousd/node</a>
          &nbsp;— firmware, backend and web, in one repo, with CI-built images you can flash straight from the browser.
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
            to it — the hardware is off-the-shelf and the software is already written.
          </p>
          <p style={s.p}>
            Everything is MIT-style permissive. Keeping a small &quot;powered by TurboUSD NodeOS&quot;
            reference is appreciated — but it&apos;s yours to build with.
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

        {/* Recent blocks + pending */}
        <div style={s.blockLane}>
          {pendingBlock && (
            <div style={{ ...s.blockTile, ...s.blockPending }}>
              <div style={s.blockNum}>#{pendingBlock.block_number}</div>
              <div style={{ fontSize: 15, fontWeight: 'bold', color: C.yellow, fontVariantNumeric: 'tabular-nums' }}>{countdown}</div>
              <div style={{ fontSize: 10, color: C.muted }}>mining…</div>
            </div>
          )}
          {minedNewestFirst.map(b => (
            <div key={b.block_number} style={{ ...s.blockTile, ...s.blockMined }}>
              <div style={s.blockNum}>#{b.block_number}</div>
              <div style={{ fontSize: 14, fontWeight: 'bold', color: C.green }}>₸{b.reward_tusd}</div>
              <div style={{ fontSize: 10, color: C.text, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap', maxWidth: 84 }}>
                {b.winner_display_name ?? '—'}
              </div>
            </div>
          ))}
        </div>

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
          <a href={setupHref} style={s.ctaSecondary}>Flash NodeOS →</a>
        </div>
        <p style={{ fontSize: 11, color: C.muted, marginTop: 22 }}>
          ₸USD rewards are for fun, not financial advice. The only guaranteed yield is a very cool desk.
        </p>
      </section>
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
  heroImgWrap: { flex: '1 1 300px', minWidth: 260, display: 'flex', justifyContent: 'center' },
  heroImg: {
    width: '100%', maxWidth: 380, height: 'auto',
    filter: 'drop-shadow(0 24px 48px rgba(67,227,151,0.12))',
  },

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
  hwImg:   { width: 260, maxWidth: '100%', height: 'auto', borderRadius: 16, flexShrink: 0 },

  statsBar: { display: 'flex', gap: 8, flexWrap: 'wrap', margin: '18px 0 16px' },
  statPill: {
    background: C.card, border: `1px solid ${C.border}`, borderRadius: 12,
    padding: '12px 8px', textAlign: 'center', flex: '1 1 0', minWidth: 110,
  },

  blockLane: { display: 'flex', gap: 8, overflowX: 'auto', padding: '4px 0 10px', scrollbarWidth: 'thin' as const },
  blockTile: {
    minWidth: 92, padding: '10px 10px 9px', borderRadius: 8, textAlign: 'center', flexShrink: 0,
    display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 3,
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

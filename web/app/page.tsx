'use client'

// app/network/page.tsx — network.turbousd.com

import { useEffect, useState, useCallback, useMemo, useRef } from 'react'
import { supabase } from '@/lib/supabase'
import SiteHeader from '@/components/SiteHeader'

// ── Brand tokens ──────────────────────────────────────────────────────────────
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

const BLOCK_INTERVAL_MS = 60 * 60 * 1000  // 1 hour

// ── Types ─────────────────────────────────────────────────────────────────────
interface NodeRow {
  node_code:         string
  display_name:      string
  bio:               string | null
  is_verified:       boolean
  is_genesis:        boolean
  is_online:         boolean
  total_tusd_earned: number
  blocks_won:        number
  windows_online:    number
  uptime_seconds:       number | null   // since boot (resets on reboot)
  total_uptime_seconds: number | null   // cumulative across reboots (server-accumulated)
  uptime_pct:        number
  created_at:        string
  last_seen_at:      string | null
  twitter_handle:    string | null
  country:           string | null
  city:              string | null
  lat:               number | null
  lng:               number | null
}

interface MiningBlock {
  block_number:        number
  reward_tusd:         number
  winner_display_name: string | null
  winner_node_code:    string | null
  mined_at:            string | null
  created_at?:         string | null   // when the block was opened (pending countdown)
  candidates_count:    number | null
}

// ── Data fetching ─────────────────────────────────────────────────────────────
async function fetchNodes(): Promise<NodeRow[]> {
  const { data } = await supabase
    .from('public_node_directory')
    .select('*')
    .order('created_at', { ascending: true })
  return (data ?? []) as NodeRow[]
}

async function fetchBlocks(): Promise<MiningBlock[]> {
  const { data } = await supabase
    .from('public_mining_feed')
    .select('*')
    .order('block_number', { ascending: false })
    .limit(50)   // deeper history — the mined lane is horizontally scrollable now
  return (data ?? []) as MiningBlock[]
}

// ── Helpers ───────────────────────────────────────────────────────────────────
function timeSince(iso: string): string {
  const s = Math.floor((Date.now() - new Date(iso).getTime()) / 1000)
  if (s < 60)    return `${s}s ago`
  if (s < 3600)  return `${Math.floor(s / 60)}m ago`
  if (s < 86400) return `${Math.floor(s / 3600)}h ago`
  const d = Math.floor(s / 86400)
  return d === 1 ? '1 day ago' : `${d} days ago`
}

// windows_online = number of 60-min mining windows the node was online for →
// approximate real online time ("14h", "3d 2h").
// Device-reported uptime (seconds since boot) → "22m" / "8h" / "3d 4h" —
// the SAME figure the device's Network screen shows, so they always agree.
function fmtUptimeSecs(secs: number): string {
  if (secs < 60) return `${secs}s`
  if (secs < 3600) return `${Math.floor(secs / 60)}m`
  if (secs < 86400) return `${Math.floor(secs / 3600)}h ${Math.floor((secs % 3600) / 60)}m`
  return `${Math.floor(secs / 86400)}d ${Math.floor((secs % 86400) / 3600)}h`
}

// Cumulative uptime across reboots (server-accumulated); falls back to the
// since-boot figure until the total_uptime_seconds migration has run.
function totalUptime(node: NodeRow): number {
  return node.total_uptime_seconds ?? node.uptime_seconds ?? 0
}

function fmtOnlineHours(windows: number): string {
  if (windows < 24) return `${windows}h`
  const d = Math.floor(windows / 24), h = windows % 24
  return h > 0 ? `${d}d ${h}h` : `${d}d`
}

function memberDuration(iso: string): string {
  const d = Math.floor((Date.now() - new Date(iso).getTime()) / 86400000)
  if (d === 0) return 'today'
  if (d < 30)  return `${d}d`
  const m = Math.floor(d / 30)
  return m === 1 ? '1 mo' : `${m} mo`
}

function fmtCountdown(ms: number): string {
  const total = Math.max(0, Math.floor(ms / 1000))
  const m = Math.floor(total / 60)
  const s = total % 60
  return `${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`
}

// ── Drag-to-scroll for the mined-blocks lane ──────────────────────────────────
// Mouse users grab the lane and drag it sideways (touch scrolls natively).
// The pending block sits OUTSIDE the lane, so it never moves. A real drag
// (>5 px) marks the lane so the click that fires on release doesn't open the
// block link under the cursor.
function laneDragStart(e: React.MouseEvent<HTMLDivElement>) {
  const el = e.currentTarget
  const startX = e.clientX
  const startScroll = el.scrollLeft
  el.dataset.dragged = ''
  const move = (ev: MouseEvent) => {
    const dx = ev.clientX - startX
    if (Math.abs(dx) > 5) el.dataset.dragged = '1'
    el.scrollLeft = startScroll - dx
    ev.preventDefault()
  }
  const up = () => {
    window.removeEventListener('mousemove', move)
    window.removeEventListener('mouseup', up)
  }
  window.addEventListener('mousemove', move)
  window.addEventListener('mouseup', up)
  e.preventDefault()
}

function laneSuppressClickAfterDrag(e: React.MouseEvent<HTMLDivElement>) {
  const el = e.currentTarget
  if (el.dataset.dragged === '1') {
    el.dataset.dragged = ''
    e.preventDefault()
    e.stopPropagation()
  }
}

// ── Main component ────────────────────────────────────────────────────────────
export default function NetworkPage() {
  const [nodes,        setNodes]        = useState<NodeRow[]>([])
  const [blocks,       setBlocks]       = useState<MiningBlock[]>([])
  const [selectedNode, setSelectedNode] = useState<NodeRow | null>(null)
  const [leaderSort,   setLeaderSort]   = useState<'rewards' | 'uptime'>('rewards')
  const [winW,         setWinW]         = useState(1200)
  useEffect(() => {
    const update = () => setWinW(window.innerWidth)
    update()
    window.addEventListener('resize', update)
    return () => window.removeEventListener('resize', update)
  }, [])

  // Install-to-home-screen state
  const [deferredPrompt, setDeferredPrompt] = useState<Event | null>(null)
  const [showInstall,    setShowInstall]    = useState(false)
  const [isIos,          setIsIos]          = useState(false)

  useEffect(() => {
    // Don't prompt if already running as installed PWA
    const isStandalone =
      window.matchMedia('(display-mode: standalone)').matches ||
      ('standalone' in window.navigator && (window.navigator as any).standalone === true)
    if (isStandalone) return

    // Only show install prompt on mobile
    if (window.innerWidth >= 768) return

    // Don't show if user already dismissed
    if (localStorage.getItem('turbousd_pwa_dismissed')) return

    const ua = navigator.userAgent.toLowerCase()
    const ios = /iphone|ipad|ipod/.test(ua)
    setIsIos(ios)

    if (ios) {
      // iOS: only prompt in Safari (not Chrome/Firefox wrappers)
      const isSafari = /safari/.test(ua) && !/crios|fxios/.test(ua)
      if (isSafari) setShowInstall(true)
    } else {
      // Android/Chrome: capture the browser's native install prompt
      const handler = (e: Event) => {
        e.preventDefault()
        setDeferredPrompt(e)
        setShowInstall(true)
      }
      window.addEventListener('beforeinstallprompt', handler)
      return () => window.removeEventListener('beforeinstallprompt', handler)
    }
  }, [])
  const [nowMs,        setNowMs]        = useState(Date.now())

  const refresh = useCallback(async () => {
    const [n, b] = await Promise.all([fetchNodes(), fetchBlocks()])
    setNodes(n)
    setBlocks(b)
  }, [])

  useEffect(() => {
    refresh()
    const t = setInterval(refresh, 30_000)
    return () => clearInterval(t)
  }, [refresh])

  // Single clock tick — drives countdown text AND circle progress
  useEffect(() => {
    const t = setInterval(() => setNowMs(Date.now()), 1000)
    return () => clearInterval(t)
  }, [])

  // nextBlockAt: last mined block timestamp + 1 hour. Before ANYTHING has
  // been mined (only the first pending block exists), fall back to the
  // pending block's created_at so the countdown ring still runs.
  const nextBlockAt = useMemo<Date | null>(() => {
    // Anchor the countdown on the PENDING block's own open time. That is the
    // value the backend restarts when a block goes unmined (no node online), so
    // the ring visibly counts down again instead of freezing at 00:00. Fall
    // back to the last mined block only if no pending block carries a timestamp.
    const pending = blocks.find(b => b.mined_at == null)
    if (pending?.created_at) return new Date(new Date(pending.created_at).getTime() + BLOCK_INTERVAL_MS)
    const last = blocks.find(b => b.mined_at != null)
    if (last?.mined_at) return new Date(new Date(last.mined_at).getTime() + BLOCK_INTERVAL_MS)
    return null
  }, [blocks])

  const msLeft       = nextBlockAt ? Math.max(0, nextBlockAt.getTime() - nowMs) : 0
  const countdown    = nextBlockAt ? fmtCountdown(msLeft) : '--:--'
  const circlePct    = nextBlockAt ? msLeft / BLOCK_INTERVAL_MS : 0      // 1→0
  const minsLeft     = Math.ceil(msLeft / 60_000)

  // Weekly node growth (cumulative) from existing nodes data
  const weeklyGrowth = useMemo(() => {
    if (!nodes.length) return [] as { week: string; total: number }[]
    const byWeek = new Map<string, number>()
    nodes.forEach(n => {
      const d = new Date(n.created_at)
      const sun = new Date(d)
      sun.setDate(d.getDate() - d.getDay())
      const key = sun.toISOString().slice(0, 10)
      byWeek.set(key, (byWeek.get(key) ?? 0) + 1)
    })
    const sorted = Array.from(byWeek.entries()).sort((a, b) => a[0].localeCompare(b[0]))
    let cum = 0
    return sorted.map(([week, n]) => { cum += n; return { week, total: cum } })
  }, [nodes])

  const onlineCount   = nodes.filter(n => n.is_online).length
  const verifiedCount = nodes.filter(n => n.is_verified).length

  const activeList = [...nodes]
    .filter(n => n.is_online)
    .sort((a, b) => new Date(a.created_at).getTime() - new Date(b.created_at).getTime())

  const leaderboard = leaderSort === 'rewards'
    ? [...nodes].sort((a, b) => b.total_tusd_earned - a.total_tusd_earned)
    : [...nodes].sort((a, b) => b.uptime_pct - a.uptime_pct)

  // Ticker layout: mined blocks flow on the LEFT (newest pushes the rest
  // leftwards), the pending block sits FIXED on the right behind a dashed
  // divider. No looping marquee — with few blocks it just showed the same
  // card repeated.
  const pendingBlock     = blocks.find(b => b.mined_at == null) ?? null
  const minedOldestFirst = blocks.filter(b => b.mined_at != null)
                                 .sort((a, b) => a.block_number - b.block_number)

  async function handleInstall() {
    if (!deferredPrompt) return
    ;(deferredPrompt as any).prompt()
    const { outcome } = await (deferredPrompt as any).userChoice
    if (outcome === 'accepted') {
      setShowInstall(false)
      setDeferredPrompt(null)
    }
  }

  function dismissInstall() {
    localStorage.setItem('turbousd_pwa_dismissed', '1')
    setShowInstall(false)
  }

  return (
    <div style={s.root}>

      {/* ── Header ── */}
      <SiteHeader />

      {/* ── Countdown bar ── */}
      {nextBlockAt && (
        <div style={s.countdownBar}>
          <span style={s.countdownLabel}>Next block in</span>
          <span style={s.countdownTimer}>{countdown}</span>
          <span style={s.countdownReward}>→ 100 ₸USD</span>
        </div>
      )}

      {/* ── Block ticker: mined lane (left) | dashed divider | pending (fixed right) ── */}
      {/* Hidden-scrollbar rule for the drag-scrollable mined lane (WebKit
          needs a real stylesheet — no inline ::-webkit-scrollbar). */}
      <style>{`.tusd-lane::-webkit-scrollbar{display:none}`}</style>
      <div style={s.tickerWrap} aria-hidden="true">
        <div style={{ display: 'flex', alignItems: 'stretch', justifyContent: 'center' }}>
          <div style={s.tickerMinedLane} className="tusd-lane"
            onMouseDown={laneDragStart}
            onClickCapture={laneSuppressClickAfterDrag}
            ref={el => { if (el && el.dataset.autoscrolled !== String(minedOldestFirst.length)) { el.scrollLeft = el.scrollWidth; el.dataset.autoscrolled = String(minedOldestFirst.length) } }}>
            {minedOldestFirst.length === 0
              ? <span style={{ alignSelf: 'center', color: C.muted, fontSize: 11, whiteSpace: 'nowrap' }}>No blocks mined yet, first one below ↓</span>
              : minedOldestFirst.map(b => (
                  <div key={b.block_number} style={{ animation: 'blockIn .6s ease', flexShrink: 0 }}>
                    <BlockTile block={b} circlePct={circlePct} minsLeft={minsLeft} />
                  </div>
                ))}
          </div>
          <div style={s.tickerDivider} />
          {pendingBlock && (
            <div style={{ padding: '12px 10px 12px 6px', flexShrink: 0 }}>
              <BlockTile block={pendingBlock} circlePct={circlePct} minsLeft={minsLeft} />
            </div>
          )}
        </div>
      </div>

      {/* ── Stats bar ── */}
      <div style={s.statsBar}>
        <StatPill label="Total nodes" value={nodes.length}   color={C.text}    />
        <StatPill label="Online now"  value={onlineCount}    color={C.green}   />
        <StatPill label="Verified"    value={verifiedCount}  color="#1d9bf0"   />
      </div>

      <div style={s.content}>

      {/* ── Get notified banner ── */}
      <GetNotifiedBanner />

      {/* ── Node Map ── */}
      <NodeMap nodes={nodes} onSelect={setSelectedNode} />

        {/* ── Nodes Online ── */}
        <section style={s.section}>
          <h2 style={s.sectionTitle}>
            <span style={{ color: C.green, fontSize: 9 }}>●</span>
            Nodes Online
            <span style={s.count}>{onlineCount}</span>
          </h2>
          {activeList.length === 0
            ? <p style={s.empty}>No nodes online.</p>
            : activeList.map(node => (
              <OnlineNodeCard key={node.node_code} node={node} wide={winW >= 640} onClick={() => setSelectedNode(node)} />
            ))
          }
        </section>

        {/* ── Network growth sparkline ── */}
        {weeklyGrowth.length > 1 && (
          <NetworkGrowthSparkline data={weeklyGrowth} totalNodes={nodes.length} />
        )}

        {/* ── Leaderboard ── */}
        <section style={s.section}>
          <h2 style={s.sectionTitle}>Leaderboard</h2>

          {nodes.length === 0
            ? <p style={s.empty}>No nodes registered yet.</p>
            : winW >= 640
              ? /* Desktop: two side-by-side tables */
                <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 16 }}>
                  <LeaderColumn
                    title="₸ Rewards"
                    nodes={[...nodes].sort((a, b) => b.total_tusd_earned - a.total_tusd_earned)}
                    right={(node: NodeRow) => (
                      <div style={{ textAlign: 'right', flexShrink: 0 }}>
                        <div style={{ fontSize: 13, fontWeight: 'bold', color: C.green }}>₸{node.total_tusd_earned.toFixed(2)}</div>
                        <div style={{ fontSize: 10, color: C.muted, marginTop: 1 }}>{node.blocks_won} blocks</div>
                      </div>
                    )}
                    onSelect={setSelectedNode}
                  />
                  <LeaderColumn
                    title="Uptime"
                    nodes={[...nodes].sort((a, b) => b.uptime_pct - a.uptime_pct)}
                    right={(node: NodeRow) => (
                      <div style={{ textAlign: 'right', flexShrink: 0 }}>
                        <div style={{ fontSize: 13, fontWeight: 'bold', color: node.uptime_pct >= 90 ? C.green : node.uptime_pct >= 60 ? C.yellow : C.muted }}>
                          {node.uptime_pct}%
                        </div>
                        <div style={{ fontSize: 10, color: C.muted, marginTop: 1 }}>uptime</div>
                      </div>
                    )}
                    onSelect={setSelectedNode}
                  />
                </div>
              : /* Mobile: toggle */
                <>
                  <div style={{ ...s.toggle, marginBottom: 12 }}>
                    <button style={leaderSort === 'rewards' ? { ...s.toggleBtn, ...s.toggleActive } : s.toggleBtn} onClick={() => setLeaderSort('rewards')}>₸ Rewards</button>
                    <button style={leaderSort === 'uptime'  ? { ...s.toggleBtn, ...s.toggleActive } : s.toggleBtn} onClick={() => setLeaderSort('uptime')}>Uptime</button>
                  </div>
                  {leaderboard.map((node, idx) => (
                    <NodeRowCard key={node.node_code} node={node}
                      prefix={<div style={s.rank}>{idx === 0 ? '🥇' : idx === 1 ? '🥈' : idx === 2 ? '🥉' : `#${idx + 1}`}</div>}
                      right={
                        leaderSort === 'rewards'
                          ? <div style={{ textAlign: 'right', flexShrink: 0 }}>
                              <div style={{ fontSize: 14, fontWeight: 'bold', color: C.green }}>₸{node.total_tusd_earned.toFixed(2)}</div>
                              <div style={{ fontSize: 11, color: C.muted, marginTop: 2 }}>{node.blocks_won} blocks</div>
                            </div>
                          : <div style={{ textAlign: 'right', flexShrink: 0 }}>
                              <div style={{ fontSize: 14, fontWeight: 'bold', color: node.uptime_pct >= 90 ? C.green : node.uptime_pct >= 60 ? C.yellow : C.muted }}>
                                {node.uptime_pct}%
                              </div>
                              <div style={{ fontSize: 11, color: C.muted, marginTop: 2 }}>uptime</div>
                            </div>
                      }
                      onClick={() => setSelectedNode(node)}
                    />
                  ))}
                </>
          }
        </section>
      </div>

      {selectedNode && (
        <NodeDetail node={selectedNode} onClose={() => setSelectedNode(null)} />
      )}

      {/* ── Install banner (mobile only, fixed to bottom) ── */}
      {showInstall && (
        <div style={s.installBanner}>
          <span style={{ fontSize: 22, flexShrink: 0 }}>📲</span>
          <div style={{ flex: 1, minWidth: 0 }}>
            <div style={{ fontWeight: 'bold', fontSize: 14, color: C.text }}>Add to Home Screen</div>
            <div style={{ fontSize: 12, color: C.muted, marginTop: 2 }}>
              {isIos
                ? 'Tap Share 📤 → "Add to Home Screen" for the full app'
                : 'Install for quick access, works offline too'}
            </div>
          </div>
          {!isIos && (
            <button
              onClick={handleInstall}
              style={{ padding: '7px 14px', background: C.green, color: C.onGreen, borderRadius: 20, fontWeight: 'bold', fontSize: 13, border: 'none', cursor: 'pointer', flexShrink: 0 }}
            >
              Install
            </button>
          )}
          <button
            onClick={dismissInstall}
            style={{ background: 'none', border: 'none', color: C.muted, cursor: 'pointer', fontSize: 18, padding: '0 4px', flexShrink: 0, lineHeight: 1 }}
            aria-label="Dismiss"
          >✕</button>
        </div>
      )}

      <style>{`
        @keyframes blockIn {
          from { transform: translateX(48px); opacity: 0; }
          to   { transform: translateX(0);    opacity: 1; }
        }
        body { margin: 0; background: #000; }
        button { transition: opacity .15s; }
        button:hover { opacity: .8; }
        a { transition: opacity .15s; }
        a:hover { opacity: .8; }
      `}</style>
    </div>
  )
}

// ── Block tile ─────────────────────────────────────────────────────────────────

const CIRC = 2 * Math.PI * 20  // r=20 → circumference ≈ 125.66

function BlockTile({ block, circlePct, minsLeft }: {
  block:      MiningBlock
  circlePct:  number
  minsLeft:   number
}) {
  const mined = !!block.mined_at

  const tile = (
    <div style={{ ...s.block, ...(mined ? s.blockMined : s.blockPending) }}>
      {/* Block number — always top */}
      <div style={s.blockNum}>#{block.block_number}</div>

      {mined ? (
        <>
          {/* Reward — center, prominent */}
          <div style={s.blockReward}>₸{block.reward_tusd}</div>
          {/* Winner — bottom */}
          <div style={s.blockWinner}>{block.winner_display_name ?? (block.winner_node_code ? `#${block.winner_node_code}` : '—')}</div>
        </>
      ) : (
        /* Pending: circular countdown */
        <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 2, flex: 1, justifyContent: 'center' }}>
          <svg width="52" height="52" viewBox="0 0 52 52">
            {/* track */}
            <circle cx="26" cy="26" r="20" fill="none" stroke={`${C.yellow}22`} strokeWidth="3" />
            {/* progress arc — rotated so it starts at top */}
            <circle
              cx="26" cy="26" r="20" fill="none"
              stroke={C.yellow} strokeWidth="3"
              strokeLinecap="round"
              strokeDasharray={CIRC}
              strokeDashoffset={CIRC * (1 - Math.max(0, Math.min(1, circlePct)))}
              transform="rotate(-90 26 26)"
            />
            {/* minutes remaining in center */}
            <text
              x="26" y="26"
              textAnchor="middle" dominantBaseline="central"
              fill={C.yellow} fontSize="13" fontWeight="bold"
              fontFamily="system-ui, sans-serif"
            >{minsLeft}</text>
          </svg>
          <div style={{ fontSize: 9, color: C.yellow, opacity: 0.5, letterSpacing: 0.5 }}>min left</div>
        </div>
      )}
    </div>
  )

  return mined
    ? <a href={`/block/${block.block_number}`} style={{ textDecoration: 'none' }}>{tile}</a>
    : <>{tile}</>
}

// ── Sub-components ─────────────────────────────────────────────────────────────

function StatPill({ label, value, color }: { label: string; value: number; color: string }) {
  return (
    <div style={s.statPill}>
      <div style={{ fontSize: 22, fontWeight: 'bold', color }}>{value}</div>
      <div style={{ fontSize: 10, color: C.muted, marginTop: 4, textTransform: 'uppercase', letterSpacing: 0.8 }}>{label}</div>
    </div>
  )
}


const VERIFY_HELP =
  'Verification pending.\n\nTo get verified:\n' +
  '1. Post a video on X showing this node running, tagging @turbousd\n' +
  '2. Write your node name on paper, show it matches your screen\n' +
  '3. Include the wallet holding your TUSD\n' +
  '4. We manually review and whitelist your node'

const GENESIS_HELP =
  'Genesis node ⚡\n\nOne of the founding nodes that joined the TurboUSD network at launch. ' +
  'The lightning badge is permanent. It marks the earliest supporters of the network.'

// Dark, centered info modal (the native alert() was a white browser popup
// pinned to the top — ugly on desktop, worse on mobile).
function InfoModal({ title, body, onClose }: { title: string; body: string; onClose: () => void }) {
  return (
    <div
      onClick={e => { e.stopPropagation(); onClose() }}
      style={{
        position: 'fixed', inset: 0, zIndex: 3000, background: 'rgba(0,0,0,.72)',
        display: 'flex', alignItems: 'center', justifyContent: 'center', padding: 20,
      }}
    >
      <div
        onClick={e => e.stopPropagation()}
        style={{
          background: '#101012', border: `1px solid ${C.border}`, borderRadius: 14,
          padding: '22px 24px', maxWidth: 420, width: '100%', color: C.text,
          boxShadow: '0 18px 60px rgba(0,0,0,.6)', position: 'relative',
        }}
      >
        <button onClick={onClose} aria-label="Close" style={{
          position: 'absolute', top: 10, right: 12, background: 'none', border: 'none',
          color: C.muted, fontSize: 16, cursor: 'pointer', lineHeight: 1,
        }}>✕</button>
        <div style={{ fontSize: 15, fontWeight: 700, marginBottom: 10 }}>{title}</div>
        <div style={{ fontSize: 13, color: '#c4c4cc', lineHeight: 1.55, whiteSpace: 'pre-line' }}>
          {body}
        </div>
      </div>
    </div>
  )
}

function UnverifiedBadge({ size = 10 }: { size?: number }) {
  const [open, setOpen] = useState(false)
  return (
    <>
      <span
        title="Verification pending. Tap for how to get verified"
        onClick={e => { e.stopPropagation(); e.preventDefault(); setOpen(true) }}
        style={{
          position: 'relative', display: 'inline-block', fontSize: size,
          color: '#6e7280', fontWeight: 700, flexShrink: 0, cursor: 'help',
          lineHeight: 1, padding: '0 1px',
        }}
      >
        ✓
        <span style={{
          position: 'absolute', left: '-15%', right: '-15%', top: '48%',
          borderTop: '2px solid #e5484d', transform: 'rotate(45deg)',
        }} />
      </span>
      {open && <InfoModal title="Verification pending" body={VERIFY_HELP.replace('Verification pending.\n\n', '')} onClose={() => setOpen(false)} />}
    </>
  )
}

function GenesisChip() {
  const [open, setOpen] = useState(false)
  return (
    <>
      <span
        title="Genesis node. Tap to learn more"
        onClick={e => { e.stopPropagation(); setOpen(true) }}
        style={{ fontSize: 12, color: C.yellow, fontWeight: 'bold', cursor: 'help' }}
      >⚡ genesis</span>
      {open && <InfoModal title="Genesis node ⚡" body={GENESIS_HELP.replace('Genesis node ⚡\n\n', '')} onClose={() => setOpen(false)} />}
    </>
  )
}

function GenesisBadge({ size = 11 }: { size?: number }) {
  const [open, setOpen] = useState(false)
  return (
    <>
      <span
        title="Genesis node. Tap to learn more"
        onClick={e => { e.stopPropagation(); e.preventDefault(); setOpen(true) }}
        style={{ fontSize: size, flexShrink: 0, cursor: 'help' }}
      >⚡</span>
      {open && <InfoModal title="Genesis node ⚡" body={GENESIS_HELP.replace('Genesis node ⚡\n\n', '')} onClose={() => setOpen(false)} />}
    </>
  )
}

function GetNotifiedBanner() {
  // Dismissal persists 7 days (localStorage timestamp). Start hidden and
  // decide in an effect so SSR/hydration never disagree about the DOM.
  const DISMISS_KEY = 'tg_banner_dismissed_at'
  const DISMISS_MS  = 7 * 24 * 60 * 60 * 1000
  const [dismissed, setDismissed] = useState(true)
  useEffect(() => {
    try {
      const at = Number(localStorage.getItem(DISMISS_KEY) ?? 0)
      setDismissed(at > 0 && Date.now() - at < DISMISS_MS)
    } catch { setDismissed(false) }
  }, [])
  const dismiss = () => {
    setDismissed(true)
    try { localStorage.setItem(DISMISS_KEY, String(Date.now())) } catch { /* private mode */ }
  }
  if (dismissed) return null
  return (
    <div style={s.notifBanner}>
      {/* Dismiss pinned to the corner so it doesn't eat a column of width */}
      <button
        onClick={dismiss}
        style={{ position: 'absolute', top: 4, right: 6, background: 'none', border: 'none', color: C.muted, cursor: 'pointer', fontSize: 13, padding: 2, lineHeight: 1 }}
        aria-label="Dismiss"
      >✕</button>
      <span style={{ fontSize: 16 }}>📱</span>
      <div style={{ flex: 1, minWidth: 0 }}>
        <div style={{ fontSize: 12, fontWeight: 'bold', color: C.text }}>
          Mining alerts on Telegram{' '}
          <span style={{ fontWeight: 'normal', color: C.muted }}>
            DM <a href="https://t.me/ami9000_bot" target="_blank" rel="noreferrer" style={{ color: C.blue, fontWeight: 'bold', textDecoration: 'none' }}>@ami9000_bot</a>:{' '}
            <code style={{ background: C.surface, padding: '1px 4px', borderRadius: 4, fontSize: 10 }}>/mynode YOUR_CODE</code>
          </span>
        </div>
      </div>
      <a
        href="https://t.me/ami9000_bot"
        target="_blank" rel="noreferrer"
        style={{ padding: '6px 14px', background: C.blue, color: '#fff', borderRadius: 20, fontSize: 12, fontWeight: 'bold', textDecoration: 'none', flexShrink: 0, marginRight: 10 }}
      >
        Open →
      </a>
    </div>
  )
}

// ── NodeMap ───────────────────────────────────────────────────────────────────
// Renders a Leaflet map loaded dynamically (client-side only).
// Only nodes that have lat/lng set (auto-detected from IP on registration)
// appear as markers. Online nodes are bright green; offline nodes are grey.
function NodeMap({ nodes, onSelect }: { nodes: NodeRow[]; onSelect: (n: NodeRow) => void }) {
  const containerRef = useRef<HTMLDivElement>(null)
  const mapRef       = useRef<any>(null)
  const onSelectRef  = useRef(onSelect)
  onSelectRef.current = onSelect

  const geoNodes = useMemo(() => nodes.filter(n => n.lat != null && n.lng != null), [nodes])

  useEffect(() => {
    if (!containerRef.current || geoNodes.length === 0) return

    function buildMarkers(L: any) {
      if (!mapRef.current) return
      // Clear old node markers
      mapRef.current.eachLayer((layer: any) => {
        if (layer._isNodeMarker) mapRef.current.removeLayer(layer)
      })
      geoNodes.forEach(node => {
        const online = node.is_online
        const marker = L.circleMarker([node.lat, node.lng], {
          radius:      online ? 7 : 5,
          color:       online ? '#43e397' : '#444',
          fillColor:   online ? '#43e397' : '#333',
          fillOpacity: online ? 0.9 : 0.55,
          weight:      online ? 2 : 1,
        })
        marker._isNodeMarker = true

        // Privacy: the map shows the COUNTRY only, never the city.
        const loc = node.country ?? ''
        const memberSince = node.created_at
          ? Math.floor((Date.now() - new Date(node.created_at).getTime()) / 86400000)
          : 0
        const memberStr = memberSince === 0 ? 'today' : memberSince < 30 ? `${memberSince}d` : `${Math.floor(memberSince / 30)}mo`

        const popupHtml = `
          <div style="font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;min-width:200px;background:#111;border:1px solid #222;border-radius:12px;padding:14px 16px;box-shadow:0 8px 32px #000a">
            <div style="display:flex;align-items:center;gap:8px;margin-bottom:10px">
              <span style="width:8px;height:8px;border-radius:50%;background:${online ? '#43e397' : '#555'};flex-shrink:0"></span>
              <span style="font-weight:700;font-size:14px;color:#e8e8e8;overflow:hidden;text-overflow:ellipsis;white-space:nowrap">${node.display_name || node.node_code}</span>
              ${node.is_verified ? '<span style="font-size:11px;color:#5b8dee;flex-shrink:0">✓</span>' : ''}
            </div>
            ${loc ? `<div style="font-size:12px;color:#6e7280;margin-bottom:10px">${loc}</div>` : ''}
            <div style="display:grid;grid-template-columns:1fr 1fr 1fr;gap:6px;margin-bottom:12px">
              <div style="background:#181818;border-radius:7px;padding:8px 8px">
                <div style="font-size:9px;color:#6e7280;text-transform:uppercase;letter-spacing:.5px">Rewards</div>
                <div style="font-size:12px;font-weight:700;color:#43e397;margin-top:2px">₸${node.total_tusd_earned.toFixed(1)}</div>
              </div>
              <div style="background:#181818;border-radius:7px;padding:8px 8px">
                <div style="font-size:9px;color:#6e7280;text-transform:uppercase;letter-spacing:.5px">Uptime</div>
                <div style="font-size:12px;font-weight:700;color:#e8e8e8;margin-top:2px">${node.uptime_pct}%</div>
              </div>
              <div style="background:#181818;border-radius:7px;padding:8px 8px">
                <div style="font-size:9px;color:#6e7280;text-transform:uppercase;letter-spacing:.5px">Member</div>
                <div style="font-size:12px;font-weight:700;color:#e8e8e8;margin-top:2px">${memberStr}</div>
              </div>
            </div>
            <a href="/node/${node.node_code}" style="display:block;text-align:center;background:#1c1c1c;border:1px solid #2a2a2a;border-radius:8px;padding:8px;font-size:12px;font-weight:600;color:#e8e8e8;text-decoration:none">View profile →</a>
          </div>
        `
        marker.bindPopup(popupHtml, {
          className:   'turbousd-popup',
          closeButton: false,
          maxWidth:    260,
          offset:      [0, -2],
        })
        marker.addTo(mapRef.current)
      })
    }

    function initMap() {
      const L = (window as any).L
      if (!L || !containerRef.current) return
      if (mapRef.current) {
        buildMarkers(L)
        return
      }
      const map = L.map(containerRef.current, {
        center:          [25, 10],
        zoom:            2,
        minZoom:         1,
        scrollWheelZoom: false,
        worldCopyJump:   true,
        zoomControl:     true,
      })
      L.tileLayer(
        'https://server.arcgisonline.com/ArcGIS/rest/services/Canvas/World_Dark_Gray_Base/MapServer/tile/{z}/{y}/{x}',
        { attribution: 'Tiles © Esri &mdash; Esri, DeLorme, NAVTEQ', maxZoom: 16 }
      ).addTo(map)
      mapRef.current = map
      buildMarkers(L)
    }

    if ((window as any).L) {
      initMap()
    } else {
      // Inject Leaflet CSS + JS once
      if (!document.getElementById('leaflet-css')) {
        const link = document.createElement('link')
        link.id   = 'leaflet-css'
        link.rel  = 'stylesheet'
        link.href = 'https://unpkg.com/leaflet@1.9.4/dist/leaflet.css'
        document.head.appendChild(link)
      }
      if (!document.getElementById('leaflet-js')) {
        const script    = document.createElement('script')
        script.id       = 'leaflet-js'
        script.src      = 'https://unpkg.com/leaflet@1.9.4/dist/leaflet.js'
        script.onload   = initMap
        document.head.appendChild(script)
      }
    }

    return () => {
      if (mapRef.current) {
        mapRef.current.remove()
        mapRef.current = null
      }
    }
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [geoNodes])

  if (geoNodes.length === 0) return null

  const onlineGeo = geoNodes.filter(n => n.is_online).length

  return (
    <section style={s.section}>
      <h2 style={s.sectionTitle}>
        <span style={{ color: C.green, fontSize: 9 }}>●</span>
        Node Map
        <span style={s.count}>{onlineGeo} online</span>
      </h2>
      {/* Leaflet popup + container overrides */}
      <style>{`
        .turbousd-popup .leaflet-popup-content-wrapper{background:transparent!important;border:none!important;box-shadow:none!important;padding:0!important;border-radius:0!important}
        .turbousd-popup .leaflet-popup-content{margin:0!important}
        .turbousd-popup .leaflet-popup-tip-container{display:none!important}
        .leaflet-container{background:#0d0d0d!important;font-family:inherit}
        .leaflet-control-zoom a{background:#1c1c1c!important;color:#aaa!important;border-color:#2a2a2a!important}
        .leaflet-control-zoom a:hover{background:#242424!important;color:#e8e8e8!important}
        .leaflet-control-attribution{background:rgba(0,0,0,.5)!important;color:#444!important;font-size:9px!important}
        .leaflet-control-attribution a{color:#555!important}
      `}</style>
      <div
        ref={containerRef}
        style={{ height: 380, borderRadius: 12, overflow: 'hidden', border: `1px solid ${C.border}` }}
      />
      <p style={{ fontSize: 11, color: C.muted, marginTop: 8, opacity: 0.7 }}>
        Location is auto-detected from each device&apos;s IP and blurred to country level. Markers are intentionally NOT exact.
      </p>
    </section>
  )
}

function NetworkGrowthSparkline({ data, totalNodes }: {
  data:       { week: string; total: number }[]
  totalNodes: number
}) {
  const W = 300
  const H = 52
  const maxVal = data[data.length - 1]?.total ?? 1
  const pts = data
    .map((d, i) => `${(i / Math.max(1, data.length - 1)) * W},${H - (d.total / maxVal) * (H - 6)}`)
    .join(' ')
  const first = data[0]?.week ? new Date(data[0].week + 'T00:00:00Z').toLocaleDateString('en-GB', { month: 'short', year: '2-digit' }) : ''

  return (
    <section style={{ ...s.section, marginBottom: 28 }}>
      <h2 style={s.sectionTitle}>
        Network growth
        <span style={s.count}>{totalNodes} total</span>
      </h2>
      <div style={{ background: C.card, border: `1px solid ${C.border}`, borderRadius: 10, padding: '14px 16px 10px' }}>
        <svg width="100%" height={H} viewBox={`0 0 ${W} ${H}`} preserveAspectRatio="none">
          <defs>
            <linearGradient id="sg" x1="0" y1="0" x2="0" y2="1">
              <stop offset="0%" stopColor={C.green} stopOpacity="0.25" />
              <stop offset="100%" stopColor={C.green} stopOpacity="0" />
            </linearGradient>
          </defs>
          <polygon points={`0,${H} ${pts} ${W},${H}`} fill="url(#sg)" />
          <polyline points={pts} fill="none" stroke={C.green} strokeWidth="2" strokeLinejoin="round" />
        </svg>
        <div style={{ display: 'flex', justifyContent: 'space-between', marginTop: 6 }}>
          <span style={{ fontSize: 10, color: C.muted }}>{first}</span>
          <span style={{ fontSize: 10, color: C.muted }}>Now</span>
        </div>
      </div>
    </section>
  )
}

function OnlineNodeCard({ node, onClick, wide }: { node: NodeRow; onClick: () => void; wide?: boolean }) {
  const firstOnline = new Date(node.created_at).toLocaleDateString('en-GB', {
    day: 'numeric', month: 'short', year: 'numeric',
  })
  // On desktop (`wide`) the stat chips move to the RIGHT of the name — the
  // card has plenty of free width there. On mobile they stay stacked below.
  const stats = (
    <div style={{
      display: 'flex', gap: wide ? 20 : 14,
      ...(wide
        ? { marginLeft: 'auto', flexShrink: 0, alignItems: 'center', flexWrap: 'nowrap' as const }
        : { marginTop: 5, flexWrap: 'wrap' as const }),
    }}>
      <StatChip label="Since" value={firstOnline} />
      <StatChip label="Uptime" value={totalUptime(node) > 0 ? fmtUptimeSecs(totalUptime(node)) : '—'} />
      <StatChip label="Blocks" value={String(node.blocks_won)} color={node.blocks_won > 0 ? C.green : undefined} />
      <StatChip label="Earned" value={`₸${node.total_tusd_earned.toFixed(1)}`} color={node.total_tusd_earned > 0 ? C.green : undefined} />
    </div>
  )
  return (
    <div style={s.nodeRow} onClick={onClick} role="button" tabIndex={0}
      onKeyDown={e => e.key === 'Enter' && onClick()}>
      {/* Online pulse */}
      <div style={{
        width: 7, height: 7, borderRadius: '50%', flexShrink: 0,
        background: C.green, boxShadow: `0 0 6px ${C.green}88`,
      }} />

      {/* Main content */}
      <div style={{ flex: 1, minWidth: 0, ...(wide ? { display: 'flex', alignItems: 'center', gap: 16 } : {}) }}>
        {/* Row 1: name + badges + code */}
        <div style={{ display: 'flex', alignItems: 'center', gap: 6, flexWrap: 'wrap' as const, ...(wide ? { minWidth: 0 } : {}) }}>
          <span style={{ ...s.nodeName, color: C.text }}>
            {node.display_name || `Node #${node.node_code}`}
          </span>
          {node.is_verified
            ? <span style={{ fontSize: 11, color: '#1d9bf0', fontWeight: 700 }}>✓</span>
            : <UnverifiedBadge size={11} />}
          {node.is_genesis && <GenesisBadge size={11} />}
          {node.display_name && <span style={{ ...s.nodeCode, marginLeft: 2 }}>#{node.node_code}</span>}
        </div>
        {/* Stats — right of the name on desktop, second row on mobile */}
        {stats}
      </div>
    </div>
  )
}

function StatChip({ label, value, color }: { label: string; value: string; color?: string }) {
  return (
    <div style={{ display: 'flex', flexDirection: 'column' as const, gap: 1 }}>
      <span style={{ fontSize: 10, color: '#9a9aa2', textTransform: 'uppercase' as const, letterSpacing: 0.6 }}>{label}</span>
      <span style={{ fontSize: 13, fontWeight: 600, color: color ?? C.text }}>{value}</span>
    </div>
  )
}

function LeaderColumn({ title, nodes, right, onSelect }: {
  title:    string
  nodes:    NodeRow[]
  right:    (node: NodeRow) => React.ReactNode
  onSelect: (node: NodeRow) => void
}) {
  return (
    <div>
      <div style={{ fontSize: 10, fontWeight: 'bold', color: C.muted, textTransform: 'uppercase', letterSpacing: 1.4, marginBottom: 10,
                    height: 16, lineHeight: '16px', display: 'flex', alignItems: 'center', gap: 4, overflow: 'hidden' }}>{title}</div>
      {nodes.map((node, idx) => (
        <div key={node.node_code} style={{ ...s.nodeRow, padding: '9px 10px' }} onClick={() => onSelect(node)} role="button" tabIndex={0} onKeyDown={e => e.key === 'Enter' && onSelect(node)}>
          <div style={{ ...s.rank, fontSize: 11 }}>{idx === 0 ? '🥇' : idx === 1 ? '🥈' : idx === 2 ? '🥉' : `#${idx + 1}`}</div>
          <div style={{
            width: 6, height: 6, borderRadius: '50%', flexShrink: 0,
            background: node.is_online ? C.green : '#2a2a2a',
            boxShadow: node.is_online ? `0 0 5px ${C.green}88` : 'none',
          }} />
          <div style={{ flex: 1, minWidth: 0 }}>
            <div style={{ display: 'flex', alignItems: 'center', gap: 5, flexWrap: 'nowrap' as const, overflow: 'hidden' }}>
              {/* Plain span, NOT a link: clicking anywhere on the row (name
                  included) opens the same bottom-sheet overlay as the Nodes
                  Online list. The standalone /node/<code> page stays reachable
                  from the overlay's own link. */}
              <span style={{ fontSize: 12, fontWeight: 600, color: C.text, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                {node.display_name || `Node #${node.node_code}`}
              </span>
              {node.is_verified
                ? <span style={{ fontSize: 10, color: '#1d9bf0', fontWeight: 700, flexShrink: 0 }}>✓</span>
                : <UnverifiedBadge size={10} />}
              {node.is_genesis  && <span style={{ fontSize: 10, flexShrink: 0 }}>⚡</span>}
            </div>
            {/* Location under the name — also shown on desktop leaderboard */}
            {node.country && (
              <div style={{ fontSize: 11, color: C.muted, marginTop: 2, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                {node.country}{node.city ? ` · ${node.city}` : ''}
              </div>
            )}
          </div>
          {right(node)}
        </div>
      ))}
    </div>
  )
}

function NodeRowCard({ node, right, prefix, onClick }: {
  node:    NodeRow
  right?:  React.ReactNode
  prefix?: React.ReactNode
  onClick: () => void
}) {
  return (
    <div style={s.nodeRow} onClick={onClick} role="button" tabIndex={0}
      onKeyDown={e => e.key === 'Enter' && onClick()}>
      {prefix}
      <div style={{
        width: 7, height: 7, borderRadius: '50%', flexShrink: 0,
        background: node.is_online ? C.green : '#2a2a2a',
        boxShadow: node.is_online ? `0 0 6px ${C.green}88` : 'none',
      }} />
      <div style={{ flex: 1, minWidth: 0 }}>
        <div style={s.nodeName}>
          <span style={{ color: C.text, fontWeight: 600 }}>
            {node.display_name || `Node #${node.node_code}`}
          </span>
          {node.is_verified ? <span style={s.verifiedBadge}>✓</span> : <UnverifiedBadge size={11} />}
          {node.is_genesis  && <span style={s.genesisBadge}>⚡</span>}
          {node.display_name && <span style={s.nodeCode}>#{node.node_code}</span>}
        </div>
        {/* Leaderboard cards show the LOCATION (not the bio), left-aligned
            under the name. */}
        {node.country && (
          <div style={s.nodeMeta}>
            <span style={{ overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
              {node.country}{node.city ? ` · ${node.city}` : ''}
            </span>
          </div>
        )}
      </div>
      {right}
    </div>
  )
}

function NodeDetail({ node, onClose }: { node: NodeRow; onClose: () => void }) {
  const [lastBlock, setLastBlock] = useState<{ block_number: number; mined_at: string } | null>(null)

  useEffect(() => {
    supabase
      .from('public_mining_feed')
      .select('block_number, mined_at')
      .eq('winner_node_code', node.node_code)
      .not('mined_at', 'is', null)
      .order('block_number', { ascending: false })
      .limit(1)
      .maybeSingle()
      .then(({ data }) => setLastBlock(data as typeof lastBlock))
  }, [node.node_code])

  function shareOnX() {
    const name = node.display_name || `Node #${node.node_code}`
    const up = totalUptime(node) > 0 ? ` · ${fmtUptimeSecs(totalUptime(node))} uptime` : ''
    const text = `My node "${name}" is live on the @TurboUSD network ⛏\n${node.blocks_won} blocks won · ${node.total_tusd_earned.toFixed(2)} ₸USD earned${up}`
    const url  = `https://network.turbousd.com/node/${node.node_code}`
    window.open(`https://x.com/intent/tweet?text=${encodeURIComponent(text)}&url=${encodeURIComponent(url)}`, '_blank')
  }

  const uptimeColor = node.uptime_pct >= 90 ? C.green : node.uptime_pct >= 60 ? C.yellow : C.muted

  return (
    <>
      <div style={s.backdrop} onClick={onClose} />
      <div style={s.panel} role="dialog" aria-modal="true">
        <button style={s.closeBtn} onClick={onClose} aria-label="Close">✕</button>

        {/* Name + badges */}
        <div style={{ display: 'flex', alignItems: 'flex-start', gap: 12, marginBottom: 16 }}>
          <div style={{
            width: 10, height: 10, borderRadius: '50%', marginTop: 5, flexShrink: 0,
            background: node.is_online ? C.green : '#333',
            boxShadow: node.is_online ? `0 0 8px ${C.green}88` : 'none',
          }} />
          <div>
            <div style={{ fontSize: 18, fontWeight: 'bold', color: C.text }}>
              {node.display_name || `Node #${node.node_code}`}
            </div>
            <div style={{ display: 'flex', gap: 8, alignItems: 'center', marginTop: 4, flexWrap: 'wrap' }}>
              <span style={{ fontSize: 12, color: C.muted }}>#{node.node_code}</span>
              {node.is_verified && <span style={{ fontSize: 12, color: C.blue, fontWeight: 'bold' }}>✓ verified</span>}
              {node.is_genesis  && <GenesisChip />}
              {node.twitter_handle && (
                <a href={`https://x.com/${node.twitter_handle}`} target="_blank" rel="noreferrer"
                  style={{ fontSize: 12, color: C.green, textDecoration: 'none' }}>
                  @{node.twitter_handle}
                </a>
              )}
              {node.country && (
                <span style={{ fontSize: 12, color: C.muted }}>
                  {node.country}{node.city ? ` · ${node.city}` : ''}
                </span>
              )}
            </div>
          </div>
        </div>

        {node.bio && (
          <p style={{ color: C.muted, fontSize: 14, lineHeight: 1.7, margin: '0 0 20px' }}>{node.bio}</p>
        )}

        {/* Stats grid */}
        <div style={s.detailGrid}>
          <DetailStat label="Total earned" value={`₸${node.total_tusd_earned.toFixed(4)}`} color={C.green}  />
          <DetailStat label="Blocks won"   value={String(node.blocks_won)}                 color={C.blue}   />
          <DetailStat label="Uptime"
            value={totalUptime(node) > 0 ? fmtUptimeSecs(totalUptime(node)) : '—'}
            color={C.green} />
          <DetailStat label="Since"        value={new Date(node.created_at).toLocaleDateString('en-GB', { day: '2-digit', month: 'short', year: 'numeric' })} color={C.yellow} />
        </div>

        {/* Last block won */}
        {lastBlock && (
          <div style={s.lastBlockBox}>
            <div style={{ fontSize: 10, color: C.muted, textTransform: 'uppercase', letterSpacing: 0.8, marginBottom: 6 }}>Last block won</div>
            <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between' }}>
              <span style={{ fontSize: 14, color: C.text, fontWeight: 600 }}>Block #{lastBlock.block_number}</span>
              <span style={{ fontSize: 12, color: C.muted }}>{timeSince(lastBlock.mined_at)}</span>
            </div>
            <a href={`/block/${lastBlock.block_number}`} style={{ fontSize: 12, color: C.green, textDecoration: 'none', marginTop: 6, display: 'inline-block' }}>
              View in explorer →
            </a>
          </div>
        )}

        {/* Share on X */}
        <button onClick={shareOnX} style={s.shareBtn}>
          <svg width="16" height="16" viewBox="0 0 24 24" fill="currentColor">
            <path d="M18.244 2.25h3.308l-7.227 8.26 8.502 11.24H16.17l-4.714-6.231-5.401 6.231H2.746l7.73-8.835L1.254 2.25H8.08l4.264 5.633 5.9-5.633zm-1.161 17.52h1.833L7.084 4.126H5.117z"/>
          </svg>
          Share on X
        </button>

        {node.last_seen_at && (
          <p style={{ fontSize: 11, color: C.muted, marginTop: 16, opacity: 0.5 }}>
            Last seen {timeSince(node.last_seen_at)}
          </p>
        )}
      </div>
    </>
  )
}

function DetailStat({ label, value, color }: { label: string; value: string; color: string }) {
  return (
    <div style={{ ...s.detailStat, display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'flex-start' }}>
      {/* Fixed lineHeight so a value containing an emoji (e.g. "⚡ 98%") lines up
          vertically with the plain-text values instead of sitting slightly lower. */}
      <div style={{ fontSize: 16, fontWeight: 'bold', color, lineHeight: '22px', height: 22 }}>{value}</div>
      <div style={{ fontSize: 10, color: C.muted, marginTop: 4, textTransform: 'uppercase', letterSpacing: 0.8 }}>{label}</div>
    </div>
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

  // Countdown
  countdownBar: {
    display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 16,
    padding: '10px 20px', background: `${C.yellow}08`, borderBottom: `1px solid ${C.yellow}18`,
  },
  countdownLabel: { fontSize: 11, color: C.muted, textTransform: 'uppercase', letterSpacing: 1 },
  countdownTimer: {
    fontSize: 32, fontWeight: 'bold', color: C.yellow, letterSpacing: -1,
    fontVariantNumeric: 'tabular-nums',
  },
  countdownReward: { fontSize: 12, color: C.muted },

  // Ticker
  tickerWrap:  { width: '100%', overflow: 'hidden', background: '#050505', borderBottom: `1px solid ${C.border}` },
  // Mined blocks: right-aligned in a clipped lane, so each newly mined block
  // appears next to the divider and pushes the older ones to the left.
  // No flex:1 — the lane shrinks to its content so the whole strip (mined +
  // divider + pending) sits CENTERED; when it grows past ~70% width it clips
  // on the left, keeping the newest blocks visible next to the divider.
  tickerMinedLane: {
    display: 'flex', gap: 8, padding: '12px 8px', overflowX: 'auto' as const, overflowY: 'hidden' as const,
    minWidth: 0, maxWidth: 'calc(100% - 180px)',
    // Scrollbar hidden — the lane drag-scrolls with the mouse (see
    // laneDragStart) and swipes natively on touch.
    scrollbarWidth: 'none' as const, msOverflowStyle: 'none' as const,
    cursor: 'grab', userSelect: 'none' as const,
  },
  tickerDivider:   { width: 0, borderLeft: '2px dashed #e8e8e8', margin: '10px 16px', opacity: 0.75 },

  block: {
    minWidth: 96, height: 110, padding: '10px 10px 8px',
    borderRadius: 8, textAlign: 'center', flexShrink: 0,
    display: 'flex', flexDirection: 'column', alignItems: 'center',
  },
  blockMined:   { background: 'linear-gradient(160deg,#081a10,rgba(39,93,59,0.57))', border: `1px solid ${C.green}55` },
  blockPending: { background: 'linear-gradient(160deg,#1a1300,rgba(93,78,39,0.57))', border: `1px solid ${C.yellow}55` },
  blockNum:     { fontSize: 12, fontWeight: 700, color: '#c4c4cc', letterSpacing: 0.5, marginBottom: 4 },
  blockReward:  { fontSize: 16, fontWeight: 'bold', color: C.green, flex: 1, display: 'flex', alignItems: 'center' },
  blockWinner: { fontSize: 12, fontWeight: 600, color: '#e8e8e8', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap', maxWidth: 96 },

  // Stats
  // nowrap + flexible pills: the three stats must share ONE line even on
  // narrow phones (the fixed 28px side padding used to push "Verified" down).
  statsBar: { display: 'flex', justifyContent: 'center', gap: 8, padding: '20px 12px 8px', flexWrap: 'nowrap' },
  statPill: { background: C.card, border: `1px solid ${C.border}`, borderRadius: 12, padding: '10px 8px', textAlign: 'center', flex: '1 1 0', maxWidth: 150, minWidth: 0 },

  // Get notified banner — inside content div, matches content width automatically
  notifBanner: {
    position: 'relative',                 // anchors the corner ✕
    display: 'flex', alignItems: 'center', gap: 10, padding: '8px 12px',
    background: `${C.blue}08`, border: `1px solid ${C.blue}40`, borderRadius: 10,
    marginBottom: 24,
  },

  // Content
  content:      { maxWidth: 800, margin: '0 auto', padding: '16px 16px 80px' },
  section:      { marginBottom: 40 },
  sectionTitle: { fontSize: 10, fontWeight: 'bold', color: C.muted, textTransform: 'uppercase', letterSpacing: 1.4, marginBottom: 12, display: 'flex', alignItems: 'center', gap: 8 },
  count:        { background: C.surface, borderRadius: 20, padding: '1px 8px', fontSize: 11, color: C.muted },
  empty:        { color: C.muted, fontSize: 13 },

  // Node rows
  nodeRow: {
    display: 'flex', alignItems: 'center', gap: 10,
    background: C.card, border: `1px solid ${C.border}`, borderRadius: 10,
    padding: '12px 14px', marginBottom: 6, cursor: 'pointer',
    transition: 'border-color .15s',
  },
  nodeName:     { fontSize: 15, fontWeight: 600, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap', color: C.text },
  verifiedBadge:{ color: C.blue, fontSize: 11, marginLeft: 6, fontWeight: 'normal' },
  genesisBadge: { fontSize: 11, marginLeft: 4 },
  nodeCode:     { color: C.muted, fontSize: 10, marginLeft: 6, fontWeight: 'normal', opacity: 0.65 },
  nodeMeta:     { display: 'flex', gap: 8, fontSize: 13, color: C.muted, marginTop: 2, overflow: 'hidden' },

  // Leaderboard toggle
  toggle:       { display: 'inline-flex', background: '#111', border: `1px solid ${C.border}`, borderRadius: 20, overflow: 'hidden', padding: 3, gap: 2 },
  toggleBtn:    { padding: '5px 16px', fontSize: 12, fontWeight: 600, background: 'transparent', color: C.muted, border: 'none', cursor: 'pointer', borderRadius: 16 },
  toggleActive: { background: '#2a2a2a', color: C.text },

  rank: { fontSize: 14, width: 28, textAlign: 'center', flexShrink: 0 },

  // Node detail panel
  backdrop: { position: 'fixed', inset: 0, background: 'rgba(0,0,0,0.72)', zIndex: 2000 },
  panel: {
    position: 'fixed', bottom: 0, left: 0, right: 0,
    background: 'rgb(24, 24, 24)', border: `1px solid ${C.border}`, borderBottom: 'none',
    borderTopLeftRadius: 20, borderTopRightRadius: 20,
    padding: '36px 24px 52px', zIndex: 2001,   // above Leaflet's panes (~1000)
    maxHeight: '80vh', overflowY: 'auto',
    maxWidth: 600, margin: '0 auto',
  },
  closeBtn: {
    position: 'absolute', top: 16, right: 20,
    background: C.surface, border: `1px solid ${C.border}`, color: C.muted,
    width: 32, height: 32, borderRadius: '50%', cursor: 'pointer', fontSize: 14,
  },
  detailGrid: { display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 10 },
  detailStat: { background: C.surface, border: `1px solid ${C.border}`, borderRadius: 10, padding: '14px 16px' },

  lastBlockBox: {
    marginTop: 14, padding: '12px 14px',
    background: C.surface, border: `1px solid ${C.border}`, borderRadius: 10,
  },

  shareBtn: {
    width: '100%', marginTop: 14, padding: '11px 0',
    background: '#000', border: '1px solid #333', borderRadius: 10,
    color: C.text, fontWeight: 'bold', fontSize: 14, cursor: 'pointer',
    display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 8,
  },

  // Install-to-home-screen bottom bar (mobile only)
  installBanner: {
    position: 'fixed', bottom: 0, left: 0, right: 0, zIndex: 150,
    display: 'flex', alignItems: 'center', gap: 12,
    padding: '14px 16px env(safe-area-inset-bottom, 16px)',
    background: 'rgba(14,14,14,0.97)', borderTop: `1px solid ${C.border}`,
    backdropFilter: 'blur(16px)',
  },
}

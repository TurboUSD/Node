'use client'

// app/network/page.tsx — network.turbousd.com

import { useEffect, useState, useCallback, useMemo, useRef } from 'react'
import { supabase } from '@/lib/supabase'
import SiteHeader from '@/components/SiteHeader'
import { LocationNote, LOCATION_HELP, VerifiedBadge } from '@/components/NodeBadges'

// ── Brand tokens ──────────────────────────────────────────────────────────────
const C = {
  green:   '#43e397',
  onGreen: '#000000',
  blue:    '#5b8dee',
  yellow:  '#ffcf72',
  red:     '#ff6b6b',
  bg:      '#000000',
  card:    '#121214',   // was #0c0c0c — cards were nearly invisible on black
  surface: '#1b1b1e',   // was #141414
  border:  '#2a2a2e',   // was #1c1c1c — edges now actually read
  text:    '#e8e8e8',
  muted:   '#9096a1',   // was #6e7280 — secondary text was too dark to read
  statVal: '#d2d2d8',   // unified stat value colour (slightly-muted white)
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
  winner_country?:     string | null   // shown at the bottom of mined tiles
  // Winner's favourite community (from node_projects), shown where the winner
  // name used to sit on mined tiles.
  winner_project_key?:    string | null
  winner_project_name?:   string | null
  winner_project_symbol?: string | null
  winner_project_count?:  number | null   // total communities the winner has; extras = count - 1
  mined_at:            string | null
  created_at?:         string | null   // when the block was opened (pending countdown)
  candidates_count:    number | null
}

// Leaderboard "By Communities" rows (public_community_leaderboard view).
interface CommunityRow {
  project_key:       string
  kind:              'token' | 'nft'
  name:              string
  symbol:            string | null
  image_url:         string | null
  chain:             string | null
  members_count:     number
  members_online:    number
  favorites_count:   number
  blocks_won:        number
  total_tusd_earned: number
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

async function fetchCommunities(): Promise<CommunityRow[]> {
  const { data } = await supabase
    .from('public_community_leaderboard')
    .select('*')
  return (data ?? []) as CommunityRow[]
}

// Every node's ★ favourite community, keyed by node_code — shown next to the
// name on the map popup and the node detail overlay.
interface FavProject {
  node_code:   string
  project_key: string
  name:        string
  symbol:      string | null
  image_url:   string | null
  count:       number   // total communities the node has (favourite included)
}

async function fetchFavorites(): Promise<Record<string, FavProject>> {
  // Pull every community so we can show the favourite AND how many more the node
  // belongs to ("(+N)" extras). One row per project; favourite has is_favorite.
  const { data } = await supabase
    .from('public_node_projects')
    .select('node_code, project_key, name, symbol, image_url, is_favorite')
  type Row = Omit<FavProject, 'count'> & { is_favorite: boolean }
  const counts: Record<string, number> = {}
  const map: Record<string, FavProject> = {}
  for (const r of (data ?? []) as Row[]) {
    counts[r.node_code] = (counts[r.node_code] ?? 0) + 1
    if (r.is_favorite) map[r.node_code] = { ...r, count: 0 }
  }
  for (const code of Object.keys(map)) map[code].count = counts[code] ?? 1
  return map
}

// Winner's favourite community label for the block tiles: the favourite's
// symbol/name, plus "(+N)" when the winner belongs to N more communities.
function projectLabel(symbol?: string | null, name?: string | null, count?: number | null): string {
  const base = symbol || name
  if (!base) return '—'
  const extra = (count ?? 0) - 1
  return extra > 0 ? `${base} (+${extra})` : base
}

// Minimal HTML escaping for user-supplied strings injected into the Leaflet
// popup (built as an HTML string, not JSX).
function escHtml(t: string): string {
  return t.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;')
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

// ── Main component ────────────────────────────────────────────────────────────
export default function NetworkPage() {
  const [nodes,        setNodes]        = useState<NodeRow[]>([])
  const [blocks,       setBlocks]       = useState<MiningBlock[]>([])
  const [communities,  setCommunities]  = useState<CommunityRow[]>([])
  const [favs,         setFavs]         = useState<Record<string, FavProject>>({})
  const [selectedNode, setSelectedNode] = useState<NodeRow | null>(null)
  const [leaderSort,   setLeaderSort]   = useState<'rewards' | 'uptime' | 'communities'>('rewards')
  const [winW,         setWinW]         = useState(1200)
  useEffect(() => {
    const update = () => setWinW(window.innerWidth)
    update()
    window.addEventListener('resize', update)
    return () => window.removeEventListener('resize', update)
  }, [])

  // ── Mined-blocks lane: same ref-based drag as the device page (the old
  // mousedown+suppress-click hack made a block "disappear"/navigate when you
  // tried to drag). A real drag (>6 px) sets `moved`, which suppresses the
  // click-to-open-block navigation so dragging never opens a block page.
  const blockLaneRef = useRef<HTMLDivElement | null>(null)
  const blockDrag    = useRef({ on: false, startX: 0, startScroll: 0, moved: false })
  function onBlockLaneDown(e: React.MouseEvent<HTMLDivElement>) {
    const el = blockLaneRef.current
    if (!el) return
    blockDrag.current = { on: true, startX: e.clientX, startScroll: el.scrollLeft, moved: false }
    const move = (ev: MouseEvent) => {
      if (!blockDrag.current.on || !blockLaneRef.current) return
      const dx = ev.clientX - blockDrag.current.startX
      if (Math.abs(dx) > 6) blockDrag.current.moved = true
      blockLaneRef.current.scrollLeft = blockDrag.current.startScroll - dx
      ev.preventDefault()
    }
    const up = () => {
      blockDrag.current.on = false
      window.removeEventListener('mousemove', move)
      window.removeEventListener('mouseup', up)
    }
    window.addEventListener('mousemove', move)
    window.addEventListener('mouseup', up)
    e.preventDefault()
  }
  function onBlockClick(e: React.MouseEvent, blockNumber: number) {
    if (blockDrag.current.moved) { e.preventDefault(); return }  // it was a drag, not a click
    window.location.href = `/block/${blockNumber}`
  }

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
    const [n, b, c, f] = await Promise.all([fetchNodes(), fetchBlocks(), fetchCommunities(), fetchFavorites()])
    setNodes(n)
    setBlocks(b)
    setCommunities(c)
    setFavs(f)
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
    : [...nodes].sort((a, b) => totalUptime(b) - totalUptime(a))

  // Communities ranked by the blocks their members are mining. Ties broken by
  // how many nodes picked the community as their ★ favourite, then by members
  // (view is already ordered that way; re-sort defensively for refreshes).
  const communityBoard = [...communities].sort((a, b) =>
    b.blocks_won - a.blocks_won
    || (b.favorites_count ?? 0) - (a.favorites_count ?? 0)
    || b.members_count - a.members_count)

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
        </div>
      )}

      {/* ── Block ticker: mined lane (left) | dashed divider | pending (fixed right) ── */}
      {/* Hidden-scrollbar rule for the drag-scrollable mined lane (WebKit
          needs a real stylesheet — no inline ::-webkit-scrollbar). */}
      <style>{`.tusd-lane::-webkit-scrollbar{display:none}`}</style>
      <div style={s.tickerWrap}>
        <div style={{ display: 'flex', alignItems: 'stretch', justifyContent: 'center' }}>
          <div style={s.tickerMinedLane} className="tusd-lane"
            onMouseDown={onBlockLaneDown}
            ref={el => {
              blockLaneRef.current = el
              if (el && el.dataset.autoscrolled !== String(minedOldestFirst.length)) {
                el.scrollLeft = el.scrollWidth   // newest parks next to the divider
                el.dataset.autoscrolled = String(minedOldestFirst.length)
              }
            }}>
            {minedOldestFirst.length === 0
              ? <span style={{ alignSelf: 'center', color: C.muted, fontSize: 11, whiteSpace: 'nowrap' }}>No blocks mined yet, first one below ↓</span>
              : minedOldestFirst.map(b => (
                  <div key={b.block_number} style={{ animation: 'blockIn .6s ease', flexShrink: 0, cursor: 'pointer' }}
                    onClick={e => onBlockClick(e, b.block_number)}>
                    <BlockTile block={b} circlePct={circlePct} minsLeft={minsLeft} />
                  </div>
                ))}
          </div>
          <div style={s.tickerDivider} />
          {pendingBlock && (
            <div style={{ padding: '12px 8px 16px 8px', flexShrink: 0 }}>
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
      <NodeMap nodes={nodes} favs={favs} onSelect={setSelectedNode} />

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
        {/* Breaks out of the 800px content column (up to 1080px, centered) so
            the three side-by-side tables get comfortably wide cards. */}
        <section style={{ ...s.section, width: 'min(1080px, calc(100vw - 32px))', marginLeft: '50%', transform: 'translateX(-50%)' }}>
          <h2 style={s.sectionTitle}>Leaderboard</h2>

          {nodes.length === 0
            ? <p style={s.empty}>No nodes registered yet.</p>
            : winW >= 640
              ? /* Desktop: side-by-side tables — three columns when the width
                   allows, otherwise Communities drops to a full-width row below */
                <div style={{ display: 'grid', gridTemplateColumns: winW >= 980 ? '1fr 1fr 1fr' : '1fr 1fr', gap: 16 }}>
                  <LeaderColumn
                    title="Total Rewards"
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
                    nodes={[...nodes].sort((a, b) => totalUptime(b) - totalUptime(a))}
                    right={(node: NodeRow) => (
                      <div style={{ textAlign: 'right', flexShrink: 0 }}>
                        <div style={{ fontSize: 13, fontWeight: 'bold', color: node.is_online ? C.green : C.muted }}>
                          {fmtUptimeSecs(totalUptime(node))}
                        </div>
                        <div style={{ fontSize: 10, color: C.muted, marginTop: 1 }}>uptime</div>
                      </div>
                    )}
                    onSelect={setSelectedNode}
                  />
                  <div style={winW >= 980 ? undefined : { gridColumn: '1 / -1' }}>
                    <CommunityColumn title="By Communities" rows={communityBoard} />
                  </div>
                </div>
              : /* Mobile: toggle */
                <>
                  <div style={{ ...s.toggle, marginBottom: 12 }}>
                    <button style={leaderSort === 'rewards' ? { ...s.toggleBtn, ...s.toggleActive } : s.toggleBtn} onClick={() => setLeaderSort('rewards')}>₸ Rewards</button>
                    <button style={leaderSort === 'uptime'  ? { ...s.toggleBtn, ...s.toggleActive } : s.toggleBtn} onClick={() => setLeaderSort('uptime')}>Uptime</button>
                    <button style={leaderSort === 'communities' ? { ...s.toggleBtn, ...s.toggleActive } : s.toggleBtn} onClick={() => setLeaderSort('communities')}>Communities</button>
                  </div>
                  {leaderSort === 'communities'
                    ? (communityBoard.length === 0
                        ? <p style={s.empty}>No communities yet. Node owners add theirs in the node settings.</p>
                        : communityBoard.map((c, idx) => (
                            <CommunityRowCard key={c.project_key} community={c} rank={idx} />
                          )))
                    : leaderboard.map((node, idx) => (
                    <NodeRowCard key={node.node_code} node={node}
                      prefix={<div style={s.rank}>{idx === 0 ? '🥇' : idx === 1 ? '🥈' : idx === 2 ? '🥉' : `#${idx + 1}`}</div>}
                      right={
                        leaderSort === 'rewards'
                          ? <div style={{ textAlign: 'right', flexShrink: 0 }}>
                              <div style={{ fontSize: 14, fontWeight: 'bold', color: C.green }}>₸{node.total_tusd_earned.toFixed(2)}</div>
                              <div style={{ fontSize: 11, color: C.muted, marginTop: 2 }}>{node.blocks_won} blocks</div>
                            </div>
                          : <div style={{ textAlign: 'right', flexShrink: 0 }}>
                              <div style={{ fontSize: 14, fontWeight: 'bold', color: node.is_online ? C.green : C.muted }}>
                                {fmtUptimeSecs(totalUptime(node))}
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
        <NodeDetail node={selectedNode} fav={favs[selectedNode.node_code]} onClose={() => setSelectedNode(null)} />
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
        /* Winner-name marquee: text is duplicated, so -50% loops seamlessly. */
        @keyframes tickerMarquee {
          from { transform: translateX(0); }
          to   { transform: translateX(-50%); }
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

// Horizontally-scrolling text ONLY when it overflows its container (e.g. a long
// winner name on a narrow block tile). Otherwise renders static.
function Marquee({ text, style }: { text: string; style?: React.CSSProperties }) {
  const ref = useRef<HTMLDivElement>(null)
  const [overflow, setOverflow] = useState(false)
  useEffect(() => {
    const el = ref.current
    if (el) setOverflow(el.scrollWidth > el.clientWidth + 2)
  }, [text])
  return (
    <div style={{ overflow: 'hidden', width: '100%', ...style }}>
      <div ref={ref} style={{
        whiteSpace: 'nowrap', display: 'inline-block',
        animation: overflow ? 'tickerMarquee 7s linear infinite' : 'none',
      }}>
        {text}{overflow && <span>{'  ·  '}{text}</span>}
      </div>
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
          {/* When it was mined */}
          <div style={s.blockAgo}>{block.mined_at ? timeSince(block.mined_at) : ''}</div>
          {/* Winner name — prominent, where the reward used to be */}
          <Marquee
            text={block.winner_display_name || (block.winner_node_code ? `#${block.winner_node_code}` : '—')}
            style={s.blockWinnerBig}
          />
          {/* Winner's favourite community + "(+N)" extra communities they added */}
          <Marquee
            text={projectLabel(block.winner_project_symbol, block.winner_project_name, block.winner_project_count)}
            style={s.blockWinner}
          />
          {/* Country under the name */}
          <div style={s.blockCountry}>{block.winner_country || ' '}</div>
        </>
      ) : (
        <>
          {/* "mining…" right under the block number */}
          <div style={s.blockAgo}>mining…</div>
          {/* Circular countdown */}
          <svg width="44" height="44" viewBox="0 0 52 52">
            <circle cx="26" cy="26" r="20" fill="none" stroke={`${C.yellow}22`} strokeWidth="3" />
            <circle
              cx="26" cy="26" r="20" fill="none"
              stroke={C.yellow} strokeWidth="3" strokeLinecap="round"
              strokeDasharray={CIRC}
              strokeDashoffset={CIRC * (1 - Math.max(0, Math.min(1, circlePct)))}
              transform="rotate(-90 26 26)"
            />
            <text x="26" y="26" textAnchor="middle" dominantBaseline="central"
              fill={C.yellow} fontSize="15" fontWeight="bold" fontFamily="system-ui, sans-serif">{minsLeft}</text>
          </svg>
          {/* Bottom label mirrors the winner line on mined tiles */}
          <div style={s.blockCountry}>Pending miner</div>
        </>
      )}
    </div>
  )

  // Navigation to /block/N is handled by the lane wrapper (with a drag guard),
  // so this just returns the tile — no <a> that would fire mid-drag.
  return tile
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
function NodeMap({ nodes, favs, onSelect }: { nodes: NodeRow[]; favs: Record<string, FavProject>; onSelect: (n: NodeRow) => void }) {
  const containerRef = useRef<HTMLDivElement>(null)
  const mapRef       = useRef<any>(null)
  const resizeRef    = useRef<(() => void) | null>(null)   // window-resize handler (re-fits the world)
  const onSelectRef  = useRef(onSelect)
  onSelectRef.current = onSelect

  const geoNodes = useMemo(() => nodes.filter(n => n.lat != null && n.lng != null), [nodes])

  // The Leaflet popup is built from an HTML string, so its location "i" can't
  // open a React modal directly. Bridge it: the popup calls window.__turboLocInfo,
  // which flips this state and renders the SAME centered InfoModal the profile
  // card uses (the inline reveal overflowed the small popup and couldn't close).
  const [locInfoOpen, setLocInfoOpen] = useState(false)
  useEffect(() => {
    ;(window as any).__turboLocInfo = () => setLocInfoOpen(true)
    return () => { delete (window as any).__turboLocInfo }
  }, [])

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
        const sinceStr = node.created_at
          ? new Date(node.created_at).toLocaleDateString('en-GB', { day: '2-digit', month: 'short', year: 'numeric' })
          : '—'
        // Second line, same shape as the overlay/public cards: id · country + info · X.
        // The id only appears here when the node HAS a name (otherwise the id is the
        // headline above). Twitter sits to the right of the country in turbo green.
        const hasName = !!node.display_name
        const twh = node.twitter_handle ? node.twitter_handle.replace(/^@/, '') : ''
        const infoSvg = `<svg width="11" height="11" viewBox="0 0 16 16" onclick="event.stopPropagation();window.__turboLocInfo&&window.__turboLocInfo()" style="vertical-align:super;cursor:pointer"><title>Approximate location — tap for details</title><circle cx="8" cy="8" r="7" fill="none" stroke="#9096a1" stroke-width="1.4"/><circle cx="8" cy="4.7" r="1.05" fill="#9096a1"/><rect x="7.05" y="6.6" width="1.9" height="5" rx="0.95" fill="#9096a1"/></svg>`
        const metaParts: string[] = []
        if (hasName) metaParts.push(`#${node.node_code}`)
        if (loc)     metaParts.push(`${loc}${infoSvg}`)
        if (twh)     metaParts.push(`<a href="https://x.com/${twh}" target="_blank" rel="noreferrer" onclick="event.stopPropagation()" style="color:#43e397;text-decoration:none">@${twh}</a>`)
        const line2 = metaParts.length ? `<div style="font-size:12px;color:#9096a1;margin-bottom:10px">${metaParts.join(' · ')}</div>` : ''

        // ★ favourite community tag, to the right of the name (links to its page),
        // with "(+N)" when the node belongs to N more communities.
        const fav = favs[node.node_code]
        const favExtra = fav ? fav.count - 1 : 0
        const favTag = fav
          ? `<a href="/community/${encodeURIComponent(fav.project_key)}" onclick="event.stopPropagation()"
               style="display:inline-flex;align-items:center;gap:4px;background:#43e39718;border:1px solid #43e39755;border-radius:20px;padding:1px 8px;font-size:10px;font-weight:700;color:#43e397;text-decoration:none;flex-shrink:0;max-width:130px;overflow:hidden;white-space:nowrap;text-overflow:ellipsis">${
                 fav.image_url ? `<img src="${escHtml(fav.image_url)}" style="width:12px;height:12px;border-radius:3px;object-fit:cover;flex-shrink:0">` : ''
               }${escHtml(fav.symbol || fav.name)}${favExtra > 0 ? ` (+${favExtra})` : ''}</a>`
          : ''
        const popupHtml = `
          <div style="font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;min-width:272px;background:#111;border:1px solid #222;border-radius:12px;padding:14px 16px;box-shadow:0 8px 32px #000a">
            <div style="display:flex;align-items:center;gap:8px;margin-bottom:10px">
              <span style="width:8px;height:8px;border-radius:50%;background:${online ? '#43e397' : '#555'};flex-shrink:0${online ? ';animation:tgNodePulse 2.4s ease-in-out infinite' : ''}"></span>
              <span style="font-weight:700;font-size:14px;color:#e8e8e8;overflow:hidden;text-overflow:ellipsis;white-space:nowrap">${node.display_name || node.node_code}</span>
              ${node.is_verified ? '<span style="font-size:11px;color:#5b8dee;flex-shrink:0">✓</span>' : ''}
              ${favTag}
            </div>
            ${line2}
            <div style="display:grid;grid-template-columns:1fr 1fr 1fr;gap:6px;margin-bottom:12px">
              <div style="background:#181818;border-radius:7px;padding:8px 8px">
                <div style="font-size:9px;color:#6e7280;text-transform:uppercase;letter-spacing:.5px">Rewards</div>
                <div style="font-size:12px;font-weight:700;color:#43e397;margin-top:2px">₸${node.total_tusd_earned.toFixed(1)}</div>
              </div>
              <div style="background:#181818;border-radius:7px;padding:8px 8px">
                <div style="font-size:9px;color:#6e7280;text-transform:uppercase;letter-spacing:.5px">Uptime</div>
                <div style="font-size:12px;font-weight:700;color:#e8e8e8;margin-top:2px;white-space:nowrap">${fmtUptimeSecs(totalUptime(node))}</div>
              </div>
              <div style="background:#181818;border-radius:7px;padding:8px 8px">
                <div style="font-size:9px;color:#6e7280;text-transform:uppercase;letter-spacing:.5px">Since</div>
                <div style="font-size:11px;font-weight:700;color:#e8e8e8;margin-top:2px;white-space:nowrap">${sinceStr}</div>
              </div>
            </div>
            <a href="/node/${node.node_code}" style="display:block;text-align:center;background:#1c1c1c;border:1px solid #2a2a2a;border-radius:8px;padding:8px;font-size:12px;font-weight:600;color:#e8e8e8;text-decoration:none">View profile →</a>
          </div>
        `
        marker.bindPopup(popupHtml, {
          className:   'turbousd-popup',
          closeButton: false,
          maxWidth:    300,
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

      // Always open framed to the WHOLE WORLD (both mobile and desktop) — never
      // zoom into wherever the nodes happen to be. fitBounds sizes the world to
      // the container, so it fits at any width/height. Re-fit on resize so it
      // stays whole-world across breakpoints/orientation changes.
      const WORLD_BOUNDS = L.latLngBounds([[-58, -170], [76, 170]])
      const fitWorld = () => {
        if (!mapRef.current) return
        mapRef.current.invalidateSize(false)
        mapRef.current.fitBounds(WORLD_BOUNDS, { animate: false, padding: [6, 6] })
      }
      fitWorld()
      // The container often gets its final size a tick after mount — re-fit once
      // more on the next frame so the first paint isn't a wrong-sized view.
      requestAnimationFrame(fitWorld)
      resizeRef.current = fitWorld
      window.addEventListener('resize', fitWorld)
      // The Leaflet popup lives inside the transformed map-pane, so NO z-index can
      // lift it above the zoom control (a separate stacking context) — most visible
      // on mobile, where the card overlaps the top-left zoom buttons. So hide the
      // zoom control while a popup is open and restore it on close.
      map.on('popupopen', () => {
        const zc = containerRef.current?.querySelector('.leaflet-control-zoom') as HTMLElement | null
        if (zc) zc.style.visibility = 'hidden'
      })
      map.on('popupclose', () => {
        const zc = containerRef.current?.querySelector('.leaflet-control-zoom') as HTMLElement | null
        if (zc) zc.style.visibility = 'visible'
      })
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
      if (resizeRef.current) {
        window.removeEventListener('resize', resizeRef.current)
        resizeRef.current = null
      }
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
        /* Node popup must sit ABOVE the zoom control (Leaflet controls are z-index 1000). */
        .leaflet-popup{z-index:1200!important}
        .leaflet-pane.leaflet-popup-pane{z-index:1200!important}
        .leaflet-container{background:#0d0d0d!important;font-family:inherit}
        .leaflet-control-zoom a{background:#1c1c1c!important;color:#aaa!important;border-color:#2a2a2a!important}
        .leaflet-control-zoom a:hover{background:#242424!important;color:#e8e8e8!important}
        .leaflet-control-attribution{background:rgba(0,0,0,.5)!important;color:#444!important;font-size:9px!important}
        .leaflet-control-attribution a{color:#555!important}
        /* Soft "alive" pulse for online node dots (matches the device footer dot). */
        @keyframes tgNodePulse{0%,100%{opacity:1}50%{opacity:.5}}
      `}</style>
      <div
        ref={containerRef}
        style={{ height: 'clamp(240px, 42vw, 320px)', borderRadius: 12, overflow: 'hidden', border: `1px solid ${C.border}` }}
      />
      <p style={{ fontSize: 11, color: C.muted, marginTop: 8, opacity: 0.7 }}>
        Location is auto-detected from each device&apos;s IP and blurred to country level. Markers are intentionally NOT exact.
      </p>
      {locInfoOpen && (
        <InfoModal title="Approximate location" body={LOCATION_HELP} onClose={() => setLocInfoOpen(false)} />
      )}
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
    day: 'numeric', month: 'short', year: '2-digit',
  })
  // FIXED-WIDTH grid so the four stats line up in the SAME columns on every
  // card (they used to shift around because flex chips are content-sized). On
  // desktop the grid sits to the RIGHT of the name; on mobile it drops below,
  // still tabulated. Same template both ways → everything reads down a column.
  const stats = (
    <div style={{
      display: 'grid',
      gridTemplateColumns: '84px 66px 46px 78px',
      columnGap: 8,
      ...(wide
        ? { marginLeft: 'auto', flexShrink: 0, alignItems: 'center' }
        : { marginTop: 8, width: '100%' }),
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
    <div style={{ display: 'flex', flexDirection: 'column' as const, gap: 1, minWidth: 0 }}>
      <span style={{ fontSize: 10, color: '#9a9aa2', textTransform: 'uppercase' as const, letterSpacing: 0.6 }}>{label}</span>
      <span style={{ fontSize: 13, fontWeight: 600, color: color ?? C.text,
        overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{value}</span>
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

// ── Communities leaderboard ───────────────────────────────────────────────────
// Ranks COMMUNITIES (tokens / NFT collections added by node owners) by the
// blocks their member nodes are mining. Rows link to /community/<key>.

function CommunityAvatar({ c, size = 22 }: { c: CommunityRow; size?: number }) {
  return c.image_url
    // eslint-disable-next-line @next/next/no-img-element
    ? <img src={c.image_url} alt="" style={{ width: size, height: size, borderRadius: 6, objectFit: 'cover', flexShrink: 0, background: '#111' }} />
    : <div style={{ width: size, height: size, borderRadius: 6, background: C.surface, border: `1px solid ${C.border}`, flexShrink: 0, display: 'flex', alignItems: 'center', justifyContent: 'center', fontSize: 10, color: C.muted }}>{c.kind === 'nft' ? '🖼' : '◆'}</div>
}

function CommunityColumn({ title, rows }: { title: string; rows: CommunityRow[] }) {
  return (
    <div>
      <div style={{ fontSize: 10, fontWeight: 'bold', color: C.muted, textTransform: 'uppercase', letterSpacing: 1.4, marginBottom: 10,
                    height: 16, lineHeight: '16px', display: 'flex', alignItems: 'center', gap: 4, overflow: 'hidden' }}>{title}</div>
      {rows.length === 0
        ? <p style={s.empty}>No communities yet.</p>
        : rows.map((c, idx) => (
            <a key={c.project_key} href={`/community/${encodeURIComponent(c.project_key)}`}
              style={{ ...s.nodeRow, padding: '9px 10px', textDecoration: 'none' }}>
              <div style={{ ...s.rank, fontSize: 11 }}>{idx === 0 ? '🥇' : idx === 1 ? '🥈' : idx === 2 ? '🥉' : `#${idx + 1}`}</div>
              <CommunityAvatar c={c} />
              <div style={{ flex: 1, minWidth: 0 }}>
                <div style={{ fontSize: 12, fontWeight: 600, color: C.text, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                  {c.name}{c.symbol ? <span style={{ color: C.muted, fontWeight: 400, marginLeft: 5, fontSize: 10 }}>{c.symbol}</span> : null}
                </div>
                <div style={{ fontSize: 11, color: C.muted, marginTop: 2, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                  {c.members_count} member{c.members_count !== 1 ? 's' : ''}{c.members_online > 0 ? ` · ${c.members_online} online` : ''}
                </div>
              </div>
              <div style={{ textAlign: 'right', flexShrink: 0 }}>
                <div style={{ fontSize: 13, fontWeight: 'bold', color: c.blocks_won > 0 ? C.yellow : C.muted }}>{c.blocks_won}</div>
                <div style={{ fontSize: 10, color: C.muted, marginTop: 1 }}>blocks</div>
              </div>
            </a>
          ))}
    </div>
  )
}

function CommunityRowCard({ community: c, rank }: { community: CommunityRow; rank: number }) {
  return (
    <a href={`/community/${encodeURIComponent(c.project_key)}`} style={{ ...s.nodeRow, textDecoration: 'none' }}>
      <div style={s.rank}>{rank === 0 ? '🥇' : rank === 1 ? '🥈' : rank === 2 ? '🥉' : `#${rank + 1}`}</div>
      <CommunityAvatar c={c} size={26} />
      <div style={{ flex: 1, minWidth: 0 }}>
        <div style={{ fontSize: 15, fontWeight: 600, color: C.text, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
          {c.name}{c.symbol ? <span style={{ color: C.muted, fontWeight: 400, marginLeft: 6, fontSize: 11 }}>{c.symbol}</span> : null}
        </div>
        <div style={{ fontSize: 13, color: C.muted, marginTop: 2, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
          {c.members_count} member{c.members_count !== 1 ? 's' : ''}{c.members_online > 0 ? ` · ${c.members_online} online` : ''}
        </div>
      </div>
      <div style={{ textAlign: 'right', flexShrink: 0 }}>
        <div style={{ fontSize: 14, fontWeight: 'bold', color: c.blocks_won > 0 ? C.yellow : C.muted }}>{c.blocks_won}</div>
        <div style={{ fontSize: 11, color: C.muted, marginTop: 2 }}>blocks</div>
      </div>
    </a>
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

function NodeDetail({ node, fav, onClose }: { node: NodeRow; fav?: FavProject; onClose: () => void }) {
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

  const detailLocation = [node.city, node.country].filter(Boolean).join(', ')

  return (
    <>
      <div style={s.backdrop} onClick={onClose} />
      <div style={s.panel} role="dialog" aria-modal="true">
        <button style={s.closeBtn} onClick={onClose} aria-label="Close">✕</button>

        {/* Line 1: online dot (soft pulse) + name + verified/unverified + genesis */}
        <div style={{ display: 'flex', alignItems: 'center', gap: 10, marginBottom: 6, flexWrap: 'wrap' }}>
          <span style={{
            width: 10, height: 10, borderRadius: '50%', flexShrink: 0,
            background: node.is_online ? C.green : '#333',
            boxShadow: node.is_online ? `0 0 8px ${C.green}88` : 'none',
            animation: node.is_online ? 'tgNodePulse 2.4s ease-in-out infinite' : undefined,
          }} />
          <span style={{ fontSize: 18, fontWeight: 'bold', color: C.text }}>
            {node.display_name || `Node #${node.node_code}`}
          </span>
          {node.is_verified ? <VerifiedBadge size={18} /> : <UnverifiedBadge size={14} />}
          {node.is_genesis && <GenesisBadge size={16} />}
          {fav && <FavTag fav={fav} />}
        </div>

        {/* Line 2: id · country + info · X handle. Inline text with " · " separators
            (matches the public node page). NOT a center-aligned flex, so the info
            icon renders as a real superscript instead of sitting on the baseline. */}
        {(() => {
          const parts: React.ReactNode[] = []
          if (node.display_name) parts.push(<span key="id">#{node.node_code}</span>)
          if (detailLocation) parts.push(<span key="loc">{detailLocation}<LocationNote /></span>)
          if (node.twitter_handle) parts.push(
            <a key="tw" href={`https://x.com/${node.twitter_handle.replace(/^@/, '')}`} target="_blank" rel="noreferrer"
              style={{ color: C.green, textDecoration: 'none' }}>@{node.twitter_handle.replace(/^@/, '')}</a>,
          )
          return parts.length > 0 ? (
            <div style={{ fontSize: 13, color: C.muted }}>
              {parts.map((p, i) => <span key={i}>{i > 0 && ' · '}{p}</span>)}
            </div>
          ) : null
        })()}

        {/* Line 3: live status — Online now if online, else last seen. */}
        <div style={{ fontSize: 13, color: C.muted, marginTop: 3, marginBottom: 16 }}>
          {node.is_online ? 'Online now' : node.last_seen_at ? `Last seen ${timeSince(node.last_seen_at)}` : 'Offline'}
        </div>

        {node.bio && (
          <p style={{ color: '#c6c6cd', fontSize: 14, lineHeight: 1.7, margin: '0 0 20px' }}>{node.bio}</p>
        )}

        {/* Stats grid — all four values share ONE muted-white colour (the old
            per-tier green/blue/yellow made the row look random). */}
        <div style={s.detailGrid}>
          <DetailStat label="Total earned" value={`₸${node.total_tusd_earned.toFixed(4)}`} color={C.statVal} />
          <DetailStat label="Blocks won"   value={String(node.blocks_won)}                 color={C.statVal} />
          <DetailStat label="Uptime"
            value={totalUptime(node) > 0 ? fmtUptimeSecs(totalUptime(node)) : '—'}
            color={C.statVal} />
          <DetailStat label="Since"        value={new Date(node.created_at).toLocaleDateString('en-GB', { day: '2-digit', month: 'short', year: 'numeric' })} color={C.statVal} />
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

        {/* Share on X + link to the public node page */}
        <div style={{ display: 'flex', gap: 10, marginTop: 14, flexWrap: 'wrap' }}>
          <button onClick={shareOnX} style={{ ...s.shareBtn, width: 'auto', flex: 1, minWidth: 130, marginTop: 0 }}>
            <svg width="16" height="16" viewBox="0 0 24 24" fill="currentColor">
              <path d="M18.244 2.25h3.308l-7.227 8.26 8.502 11.24H16.17l-4.714-6.231-5.401 6.231H2.746l7.73-8.835L1.254 2.25H8.08l4.264 5.633 5.9-5.633zm-1.161 17.52h1.833L7.084 4.126H5.117z"/>
            </svg>
            Share on X
          </button>
          <a href={`/node/${node.node_code}`} style={{
            flex: 1, minWidth: 130, padding: '11px 0', background: 'transparent',
            border: `1px solid ${C.green}`, borderRadius: 10, color: C.green,
            fontWeight: 'bold', fontSize: 14, textDecoration: 'none',
            display: 'flex', alignItems: 'center', justifyContent: 'center',
          }}>Full profile →</a>
        </div>
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

// ★ favourite community pill — shown to the right of the node name on the map
// detail overlay. Links to the community page (matches the map popup favTag).
function FavTag({ fav }: { fav: FavProject }) {
  const extra = fav.count - 1
  return (
    <a
      href={`/community/${encodeURIComponent(fav.project_key)}`}
      onClick={e => e.stopPropagation()}
      title={`${fav.name} — favorite community${extra > 0 ? ` (+${extra} more)` : ''}`}
      style={{
        display: 'inline-flex', alignItems: 'center', gap: 4, flexShrink: 0,
        maxWidth: 170, overflow: 'hidden', whiteSpace: 'nowrap', textOverflow: 'ellipsis',
        background: `${C.green}18`, border: `1px solid ${C.green}55`, borderRadius: 20,
        padding: '2px 9px', fontSize: 11, fontWeight: 700, color: C.green, textDecoration: 'none',
      }}
    >
      {fav.image_url && (
        // eslint-disable-next-line @next/next/no-img-element
        <img src={fav.image_url} alt="" style={{ width: 13, height: 13, borderRadius: 3, objectFit: 'cover', flexShrink: 0 }} />
      )}
      <span style={{ overflow: 'hidden', textOverflow: 'ellipsis' }}>{fav.symbol || fav.name}</span>
      {extra > 0 && <span style={{ flexShrink: 0, opacity: 0.85 }}>(+{extra})</span>}
    </a>
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
    display: 'flex', gap: 14, padding: '12px 5px 16px 8px', overflowX: 'auto' as const, overflowY: 'hidden' as const,
    minWidth: 0, maxWidth: 'calc(100% - 180px)',
    // Scrollbar hidden — the lane drag-scrolls with the mouse (see
    // onBlockLaneDown) and swipes natively on touch.
    scrollbarWidth: 'none' as const, msOverflowStyle: 'none' as const,
    cursor: 'grab', userSelect: 'none' as const,
  },
  tickerDivider:   { width: 0, borderLeft: '2px dashed #e8e8e8', margin: '8px 8px', opacity: 0.75 },

  block: {
    minWidth: 100, height: 104, padding: '8px 9px',
    borderRadius: 8, textAlign: 'center', flexShrink: 0,
    display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'space-between',
    // Room for the 3D shadow below without a big empty gap under the strip.
    marginBottom: 8,
  },
  // mempool.space-style 3D block: single-tone extrusion to the lower-LEFT.
  blockMined:   { background: '#1b4d2e', border: `1px solid ${C.green}66`,
                  boxShadow: '-3px 4px 0 #143f26, -6px 8px 0 #143f26, -8px 12px 0 #143f26, -9px 15px 13px rgba(0,0,0,0.5)' },
  blockPending: { background: '#4d3c15', border: `1px solid ${C.yellow}66`,
                  boxShadow: '-3px 4px 0 #3a2c0f, -6px 8px 0 #3a2c0f, -8px 12px 0 #3a2c0f, -9px 15px 13px rgba(0,0,0,0.5)' },
  blockNum:     { fontSize: 12, fontWeight: 700, color: '#e8e8ea', letterSpacing: 0.5 },
  blockAgo:     { fontSize: 9,  color: '#a4a8b2' },
  blockReward:  { fontSize: 16, fontWeight: 'bold', color: C.green },
  // Winner name — prominent, sits where the reward used to be on mined tiles
  blockWinnerBig: { fontSize: 10.5, fontWeight: 'bold', color: C.green, maxWidth: 96, textAlign: 'center' },
  // Winner's favourite community — sits where the winner name used to be
  blockWinner:  { fontSize: 11, fontWeight: 600, color: '#e8e8e8', maxWidth: 92, textAlign: 'center' },
  blockCountry: { fontSize: 9, color: '#a4a8b2', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap', maxWidth: 92 },

  // Stats
  // nowrap + flexible pills: the three stats must share ONE line even on
  // narrow phones (the fixed 28px side padding used to push "Verified" down).
  statsBar: { display: 'flex', justifyContent: 'center', gap: 8, padding: '20px 12px 8px', flexWrap: 'nowrap' },
  statPill: { background: C.surface, border: `1px solid ${C.border}`, borderRadius: 12, padding: '10px 8px', textAlign: 'center', flex: '1 1 0', maxWidth: 150, minWidth: 0 },

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
  nodeCode:     { color: C.muted, fontSize: 10, marginLeft: 6, fontWeight: 'normal', opacity: 0.85 },
  nodeMeta:     { display: 'flex', gap: 8, fontSize: 13, color: C.muted, marginTop: 2, overflow: 'hidden' },

  // Leaderboard toggle
  toggle:       { display: 'inline-flex', background: '#111', border: `1px solid ${C.border}`, borderRadius: 20, overflow: 'hidden', padding: 3, gap: 2 },
  toggleBtn:    { padding: '5px 16px', fontSize: 12, fontWeight: 600, background: 'transparent', color: C.muted, border: 'none', cursor: 'pointer', borderRadius: 16 },
  toggleActive: { background: '#2a2a2a', color: C.text },

  rank: { fontSize: 14, width: 28, textAlign: 'center', flexShrink: 0, color: C.statVal },

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

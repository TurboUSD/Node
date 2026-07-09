'use client'

// app/nodes/page.tsx — network.turbousd.com/nodes
//
// Full directory of every node that has ever registered on the network, online
// or not, verified or not. Linked from each public node page's "Node List"
// button. Sortable by join date (default, earliest first), rewards, blocks or
// uptime. Desktop shows a table; narrow screens collapse to stacked cards.

import { useEffect, useState } from 'react'
import { supabase } from '@/lib/supabase'
import { VerifiedBadge, UnverifiedBadge, GenesisBadge } from '@/components/NodeBadges'
import NodeOverlay from '@/components/NodeOverlay'

const C = {
  green:   '#43e397',
  blue:    '#5b8dee',
  bg:      '#000000',
  card:    '#0c0c0c',
  surface: '#141414',
  border:  '#1c1c1c',
  text:    '#e8e8e8',
  muted:   '#9096a1',
  statVal: '#d2d2d8',
}

interface NodeRow {
  node_code:            string
  display_name:         string | null
  is_verified:          boolean
  is_genesis:           boolean
  is_online:            boolean
  total_tusd_earned:    number
  blocks_won:           number
  total_uptime_seconds: number | null
  uptime_seconds:       number | null
  created_at:           string
  country:              string | null
}

type SortKey = 'since' | 'rewards' | 'blocks' | 'uptime'
type SortDir = 'asc' | 'desc'
const SORTS: { key: SortKey; label: string }[] = [
  { key: 'since',   label: 'Since' },
  { key: 'rewards', label: 'Rewards' },
  { key: 'blocks',  label: 'Blocks' },
  { key: 'uptime',  label: 'Uptime' },
]
// Direction applied when a sort is FIRST selected; clicking the active sort again flips it.
const DEFAULT_DIR: Record<SortKey, SortDir> = { since: 'asc', rewards: 'desc', blocks: 'desc', uptime: 'desc' }
const PAGE_SIZES = [25, 50, 100]

function uptimeOf(n: NodeRow): number {
  return n.total_uptime_seconds ?? n.uptime_seconds ?? 0
}
function fmtUptime(secs: number): string {
  if (secs <= 0) return '—'
  if (secs < 60)    return `${secs}s`
  if (secs < 3600)  return `${Math.floor(secs / 60)}m`
  if (secs < 86400) return `${Math.floor(secs / 3600)}h ${Math.floor((secs % 3600) / 60)}m`
  return `${Math.floor(secs / 86400)}d ${Math.floor((secs % 86400) / 3600)}h`
}
function joinDate(iso: string): string {
  return new Date(iso).toLocaleDateString('en-GB', { day: '2-digit', month: 'short', year: 'numeric' })
}
function nameOf(n: NodeRow): string {
  return n.display_name || `#${n.node_code}`
}

function cmpAsc(a: NodeRow, b: NodeRow, key: SortKey): number {
  switch (key) {
    case 'since':   return a.created_at.localeCompare(b.created_at)   // earliest first
    case 'rewards': return a.total_tusd_earned - b.total_tusd_earned
    case 'blocks':  return a.blocks_won - b.blocks_won
    case 'uptime':  return uptimeOf(a) - uptimeOf(b)
  }
}
function sortNodes(rows: NodeRow[], key: SortKey, dir: SortDir): NodeRow[] {
  const arr = [...rows].sort((a, b) => cmpAsc(a, b, key))
  if (dir === 'desc') arr.reverse()
  return arr
}

export default function NodesPage() {
  const [rows,    setRows]    = useState<NodeRow[]>([])
  const [sort,    setSort]    = useState<SortKey>('since')
  const [dir,     setDir]     = useState<SortDir>('asc')
  const [query,   setQuery]   = useState('')
  const [page,    setPage]    = useState(0)
  const [pageSize, setPageSize] = useState(25)
  const [overlayCode, setOverlayCode] = useState<string | null>(null)
  const [loading, setLoading] = useState(true)
  const [winW,    setWinW]    = useState(1024)

  // Any change to the result set or ordering jumps back to the first page.
  useEffect(() => { setPage(0) }, [sort, dir, query, pageSize])

  useEffect(() => {
    const on = () => setWinW(window.innerWidth)
    on(); window.addEventListener('resize', on)
    return () => window.removeEventListener('resize', on)
  }, [])

  useEffect(() => {
    setLoading(true)
    supabase
      .from('public_node_directory')
      .select('node_code,display_name,is_verified,is_genesis,is_online,total_tusd_earned,blocks_won,total_uptime_seconds,uptime_seconds,created_at,country')
      .limit(1000)
      .then(({ data }) => {
        setRows((data as NodeRow[]) ?? [])
        setLoading(false)
      })
  }, [])

  const q        = query.trim().toLowerCase()
  const filtered = q
    ? rows.filter(n => (n.display_name || '').toLowerCase().includes(q) || n.node_code.toLowerCase().includes(q))
    : rows
  const sorted   = sortNodes(filtered, sort, dir)
  const onlineCt = rows.filter(n => n.is_online).length
  const wide     = winW >= 720

  // Client-side pagination — slices the sorted+filtered set, so it's ready for
  // many entries without breaking sort/search (which need the whole set).
  const totalPages   = Math.max(1, Math.ceil(sorted.length / pageSize))
  const pageClamped   = Math.min(page, totalPages - 1)
  const paged        = sorted.slice(pageClamped * pageSize, pageClamped * pageSize + pageSize)
  const hasPrev      = pageClamped > 0
  const hasNext      = pageClamped < totalPages - 1

  function onSort(key: SortKey) {
    if (key === sort) setDir(d => (d === 'asc' ? 'desc' : 'asc'))   // flip within the same filter
    else { setSort(key); setDir(DEFAULT_DIR[key]) }
  }

  return (
    <div style={s.root}>
      {/* Soft "alive" pulse for online dots (matches the device footer dot). */}
      <style>{`@keyframes tgNodePulse{0%,100%{opacity:1}50%{opacity:.5}}`}</style>

      <header style={s.header}>
        <a href="/" style={s.back}>← Network</a>
        <span style={s.logo}>Nodes</span>
      </header>

      <div style={s.content}>
        <div style={s.titleRow}>
          <div style={{ display: 'flex', alignItems: 'baseline', gap: 10 }}>
            <h1 style={s.h1}>All nodes</h1>
            <span style={s.count}>{rows.length.toLocaleString()} total · {onlineCt} online</span>
          </div>
          {/* Sort selector (right; wraps below on narrow screens). */}
          <div style={s.sizeRow}>
            <span style={{ fontSize: 12, color: C.muted }}>Sort by</span>
            {SORTS.map(o => (
              <button key={o.key} onClick={() => onSort(o.key)}
                style={o.key === sort ? { ...s.sizeBtn, ...s.sizeBtnActive } : s.sizeBtn}>
                {o.label}{o.key === sort ? (dir === 'asc' ? ' ↑' : ' ↓') : ''}
              </button>
            ))}
          </div>
        </div>

        {/* Simple search — filters by node name or id; wraps fine on mobile. */}
        <input
          value={query}
          onChange={e => setQuery(e.target.value)}
          placeholder="Search by name or id…"
          style={s.search}
        />

        {loading && rows.length === 0
          ? <p style={s.dim}>Loading…</p>
          : rows.length === 0
            ? <p style={s.dim}>No nodes registered yet.</p>
            : sorted.length === 0
              ? <p style={s.dim}>No nodes match &ldquo;{query}&rdquo;.</p>
              : wide ? <DesktopTable rows={paged} onOpen={setOverlayCode} /> : <MobileList rows={paged} onOpen={setOverlayCode} />
        }

        {sorted.length > 0 && (
          <div style={s.navRow}>
            {hasPrev
              ? <button onClick={() => setPage(p => Math.max(0, p - 1))} style={s.navBtn}>← Previous</button>
              : <span style={s.navBtnDisabled}>← Previous</span>}
            <div style={s.pageMid}>
              <span style={s.pageInfo}>Page {pageClamped + 1} / {totalPages}</span>
              <span style={{ color: C.muted, fontSize: 12 }}>·</span>
              {PAGE_SIZES.map(nn => (
                <button key={nn} onClick={() => setPageSize(nn)}
                  style={nn === pageSize ? { ...s.sizeBtn, ...s.sizeBtnActive } : s.sizeBtn}>{nn}</button>
              ))}
            </div>
            {hasNext
              ? <button onClick={() => setPage(p => p + 1)} style={s.navBtn}>Next →</button>
              : <span style={s.navBtnDisabled}>Next →</span>}
          </div>
        )}
      </div>

      {overlayCode && <NodeOverlay nodeCode={overlayCode} onClose={() => setOverlayCode(null)} />}
    </div>
  )
}

function StatusDot({ online }: { online: boolean }) {
  return (
    <span style={{
      width: 9, height: 9, borderRadius: '50%', flexShrink: 0, display: 'inline-block',
      background: online ? C.green : '#3a3a3a',
      boxShadow: online ? `0 0 7px ${C.green}88` : 'none',
      animation: online ? 'tgNodePulse 2.4s ease-in-out infinite' : undefined,
    }} />
  )
}

function Badges({ n }: { n: NodeRow }) {
  return (
    <>
      {n.is_verified ? <VerifiedBadge size={15} /> : <UnverifiedBadge size={12} />}
      {n.is_genesis && <GenesisBadge size={13} />}
    </>
  )
}

function DesktopTable({ rows, onOpen }: { rows: NodeRow[]; onOpen: (code: string) => void }) {
  return (
    <div style={s.tableWrap}>
      <div style={{ ...s.tr, ...s.thead }}>
        <div style={{ ...s.td, ...s.cNode }}>Node</div>
        <div style={{ ...s.td, ...s.cNum }}>Earned</div>
        <div style={{ ...s.td, ...s.cNum }}>Blocks</div>
        <div style={{ ...s.td, ...s.cNum }}>Uptime</div>
        <div style={{ ...s.td, ...s.cSince }}>Since</div>
      </div>
      {rows.map(n => (
        <div key={n.node_code} onClick={() => onOpen(n.node_code)} role="button" tabIndex={0}
          onKeyDown={e => e.key === 'Enter' && onOpen(n.node_code)} style={{ ...s.tr, cursor: 'pointer' }}>
          <div style={{ ...s.td, ...s.cNode, display: 'flex', alignItems: 'center', gap: 8, minWidth: 0 }}>
            <StatusDot online={n.is_online} />
            <span style={{ ...s.ellip, fontWeight: 600 }}>{nameOf(n)}</span>
            <Badges n={n} />
          </div>
          <div style={{ ...s.td, ...s.cNum, color: C.green, fontWeight: 700 }}>₸{n.total_tusd_earned.toFixed(2)}</div>
          <div style={{ ...s.td, ...s.cNum, color: C.statVal }}>{n.blocks_won}</div>
          <div style={{ ...s.td, ...s.cNum, color: C.statVal }}>{fmtUptime(uptimeOf(n))}</div>
          <div style={{ ...s.td, ...s.cSince, color: C.muted }}>{joinDate(n.created_at)}</div>
        </div>
      ))}
    </div>
  )
}

function MobileList({ rows, onOpen }: { rows: NodeRow[]; onOpen: (code: string) => void }) {
  return (
    <div>
      {rows.map(n => (
        <div key={n.node_code} onClick={() => onOpen(n.node_code)} role="button" tabIndex={0}
          onKeyDown={e => e.key === 'Enter' && onOpen(n.node_code)} style={{ ...s.mCard, cursor: 'pointer' }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: 8, minWidth: 0 }}>
            <StatusDot online={n.is_online} />
            <span style={{ ...s.ellip, color: C.text, fontWeight: 700, fontSize: 15 }}>{nameOf(n)}</span>
            <Badges n={n} />
          </div>
          <div style={s.mMeta}>
            <span style={{ color: C.green, fontWeight: 700 }}>₸{n.total_tusd_earned.toFixed(2)}</span>
            <span>· {n.blocks_won} blocks</span>
            <span>· {fmtUptime(uptimeOf(n))} up</span>
            <span>· since {joinDate(n.created_at)}</span>
          </div>
        </div>
      ))}
    </div>
  )
}

const s: Record<string, React.CSSProperties> = {
  root:    { minHeight: '100vh', background: C.bg, color: C.text, fontFamily: 'system-ui, -apple-system, sans-serif' },
  header:  {
    display: 'flex', alignItems: 'center', justifyContent: 'space-between',
    padding: '0 20px', height: 56, borderBottom: `1px solid ${C.border}`,
    position: 'sticky', top: 0, background: 'rgba(0,0,0,0.92)', backdropFilter: 'blur(12px)', zIndex: 10,
  },
  back:    { color: C.muted, textDecoration: 'none', fontSize: 14 },
  logo:    { fontSize: 16, fontWeight: 'bold', color: C.text },
  content: { maxWidth: 900, margin: '0 auto', padding: '24px 16px 80px' },

  titleRow: { display: 'flex', alignItems: 'center', justifyContent: 'space-between', gap: 12, flexWrap: 'wrap', marginBottom: 14 },
  h1:       { fontSize: 22, margin: 0 },
  count:    { fontSize: 13, color: C.muted },
  dim:      { color: C.muted, fontSize: 14 },

  sizeRow:       { display: 'flex', alignItems: 'center', gap: 6, flexWrap: 'wrap' },
  // outline:none so a clicked (but inactive) button doesn't keep a white focus ring —
  // only the active sort should read as selected (green).
  sizeBtn:       { padding: '5px 12px', background: C.surface, border: `1px solid ${C.border}`, borderRadius: 16, color: C.muted, fontSize: 12, fontWeight: 600, cursor: 'pointer', fontFamily: 'inherit', outline: 'none' },
  sizeBtnActive: { background: C.green, color: '#000', borderColor: C.green },

  search: { width: '100%', boxSizing: 'border-box', padding: '9px 14px', marginBottom: 14, background: C.surface, border: `1px solid ${C.border}`, borderRadius: 10, color: C.text, fontSize: 14, fontFamily: 'inherit', outline: 'none' },

  navRow:         { display: 'flex', justifyContent: 'space-between', alignItems: 'center', gap: 10, flexWrap: 'wrap', marginTop: 18 },
  navBtn:         { padding: '11px 20px', background: C.surface, border: `1px solid ${C.border}`, borderRadius: 8, color: C.text, fontSize: 14, fontWeight: 700, cursor: 'pointer', fontFamily: 'inherit', outline: 'none' },
  navBtnDisabled: { padding: '11px 20px', background: C.card, border: `1px solid ${C.border}`, borderRadius: 8, color: C.muted, fontSize: 14, fontWeight: 700, opacity: 0.6 },
  pageMid:        { display: 'flex', alignItems: 'center', gap: 6, flexWrap: 'wrap', justifyContent: 'center' },
  pageInfo:       { fontSize: 13, color: C.muted },

  tableWrap: { background: C.card, border: `1px solid ${C.border}`, borderRadius: 12, overflow: 'hidden', marginBottom: 4 },
  tr:        { display: 'flex', alignItems: 'center', gap: 8, padding: '11px 12px', borderBottom: `1px solid ${C.border}`, textDecoration: 'none', color: C.text, fontSize: 13 },
  thead:     { color: C.muted, fontSize: 11, textTransform: 'uppercase', letterSpacing: 0.5, fontWeight: 700 },
  td:        { minWidth: 0 },
  ellip:     { overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' },
  cNode:     { flex: '1 1 150px' },
  cNum:      { flex: '0 0 84px', textAlign: 'right' },
  cSince:    { flex: '0 0 96px', textAlign: 'right' },

  mCard: { display: 'block', background: C.card, border: `1px solid ${C.border}`, borderRadius: 10, padding: '11px 14px', marginBottom: 8, textDecoration: 'none', color: C.text },
  mMeta: { display: 'flex', flexWrap: 'wrap', gap: 6, fontSize: 12, color: C.muted, marginTop: 6 },
}

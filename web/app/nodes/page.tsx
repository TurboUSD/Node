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
const SORTS: { key: SortKey; label: string }[] = [
  { key: 'since',   label: 'Since' },
  { key: 'rewards', label: 'Rewards' },
  { key: 'blocks',  label: 'Blocks' },
  { key: 'uptime',  label: 'Uptime' },
]

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

function sortNodes(rows: NodeRow[], key: SortKey): NodeRow[] {
  const arr = [...rows]
  switch (key) {
    // Earliest registered on top (the founders first).
    case 'since':   arr.sort((a, b) => a.created_at.localeCompare(b.created_at)); break
    case 'rewards': arr.sort((a, b) => b.total_tusd_earned - a.total_tusd_earned); break
    case 'blocks':  arr.sort((a, b) => b.blocks_won - a.blocks_won); break
    case 'uptime':  arr.sort((a, b) => uptimeOf(b) - uptimeOf(a)); break
  }
  return arr
}

export default function NodesPage() {
  const [rows,    setRows]    = useState<NodeRow[]>([])
  const [sort,    setSort]    = useState<SortKey>('since')
  const [loading, setLoading] = useState(true)
  const [winW,    setWinW]    = useState(1024)

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

  const sorted   = sortNodes(rows, sort)
  const onlineCt = rows.filter(n => n.is_online).length
  const wide     = winW >= 720

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
              <button key={o.key} onClick={() => setSort(o.key)}
                style={o.key === sort ? { ...s.sizeBtn, ...s.sizeBtnActive } : s.sizeBtn}>
                {o.label}
              </button>
            ))}
          </div>
        </div>

        {loading && rows.length === 0
          ? <p style={s.dim}>Loading…</p>
          : rows.length === 0
            ? <p style={s.dim}>No nodes registered yet.</p>
            : wide ? <DesktopTable rows={sorted} /> : <MobileList rows={sorted} />
        }
      </div>
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

function DesktopTable({ rows }: { rows: NodeRow[] }) {
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
        <a key={n.node_code} href={`/node/${n.node_code}`} style={s.tr}>
          <div style={{ ...s.td, ...s.cNode, display: 'flex', alignItems: 'center', gap: 8, minWidth: 0 }}>
            <StatusDot online={n.is_online} />
            <span style={{ ...s.ellip, fontWeight: 600 }}>{nameOf(n)}</span>
            <Badges n={n} />
          </div>
          <div style={{ ...s.td, ...s.cNum, color: C.green, fontWeight: 700 }}>₸{n.total_tusd_earned.toFixed(2)}</div>
          <div style={{ ...s.td, ...s.cNum, color: C.statVal }}>{n.blocks_won}</div>
          <div style={{ ...s.td, ...s.cNum, color: C.statVal }}>{fmtUptime(uptimeOf(n))}</div>
          <div style={{ ...s.td, ...s.cSince, color: C.muted }}>{joinDate(n.created_at)}</div>
        </a>
      ))}
    </div>
  )
}

function MobileList({ rows }: { rows: NodeRow[] }) {
  return (
    <div>
      {rows.map(n => (
        <a key={n.node_code} href={`/node/${n.node_code}`} style={s.mCard}>
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
        </a>
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
  sizeBtn:       { padding: '5px 12px', background: C.surface, border: `1px solid ${C.border}`, borderRadius: 16, color: C.muted, fontSize: 12, fontWeight: 600, cursor: 'pointer', fontFamily: 'inherit' },
  sizeBtnActive: { background: C.green, color: '#000', borderColor: C.green },

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

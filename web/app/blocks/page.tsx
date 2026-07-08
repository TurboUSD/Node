'use client'

// app/blocks/page.tsx — network.turbousd.com/blocks
//
// Full paginated list of every mined block, linked from each block page's
// "☰ All blocks" link. A quick table-style scan of block #, time, winner,
// reward, online count and the randomness hash. Long fields (winner name,
// randomness) are truncated; on narrow screens the table collapses to a
// stacked card list so nothing overflows on mobile.

import { useEffect, useState } from 'react'
import { supabase } from '@/lib/supabase'

const C = {
  green:   '#43e397',
  blue:    '#5b8dee',
  bg:      '#000000',
  card:    '#0c0c0c',
  surface: '#141414',
  border:  '#1c1c1c',
  text:    '#e8e8e8',
  muted:   '#6e7280',
}

interface BlockRow {
  block_number:        number
  mined_at:            string | null
  reward_tusd:         number
  randomness_source:   string | null
  candidates_count:    number | null
  winner_display_name: string | null
  winner_node_code:    string | null
}

const PAGE_SIZES = [25, 50, 100]

function timeSince(iso: string): string {
  const s = Math.floor((Date.now() - new Date(iso).getTime()) / 1000)
  if (s < 60)    return `${s}s ago`
  if (s < 3600)  return `${Math.floor(s / 60)}m ago`
  if (s < 86400) return `${Math.floor(s / 3600)}h ago`
  return `${Math.floor(s / 86400)}d ago`
}
function shortHash(h: string | null): string {
  if (!h) return '—'
  return h.length > 14 ? `${h.slice(0, 8)}…${h.slice(-4)}` : h
}
function winnerLabel(b: BlockRow): string {
  if (b.winner_display_name) return b.winner_display_name
  if (b.winner_node_code)    return `#${b.winner_node_code}`
  return '—'
}

export default function BlocksPage() {
  const [rows,     setRows]     = useState<BlockRow[]>([])
  const [total,    setTotal]    = useState<number | null>(null)
  const [page,     setPage]     = useState(0)
  const [pageSize, setPageSize] = useState(25)
  const [loading,  setLoading]  = useState(true)
  const [winW,     setWinW]     = useState(1024)

  useEffect(() => {
    const on = () => setWinW(window.innerWidth)
    on(); window.addEventListener('resize', on)
    return () => window.removeEventListener('resize', on)
  }, [])

  useEffect(() => {
    setLoading(true)
    const from = page * pageSize
    supabase
      .from('public_mining_feed')
      .select(
        'block_number, mined_at, reward_tusd, randomness_source, candidates_count, winner_display_name, winner_node_code',
        { count: 'exact' },
      )
      .not('mined_at', 'is', null)
      .order('block_number', { ascending: false })
      .range(from, from + pageSize - 1)
      .then(({ data, count }) => {
        setRows((data as BlockRow[]) ?? [])
        setTotal(count ?? null)
        setLoading(false)
      })
  }, [page, pageSize])

  const totalPages = total != null ? Math.max(1, Math.ceil(total / pageSize)) : null
  const hasPrev = page > 0
  const hasNext = totalPages != null ? page < totalPages - 1 : rows.length === pageSize
  const wide = winW >= 720

  return (
    <div style={s.root}>
      <header style={s.header}>
        <a href="/" style={s.back}>← Network</a>
        <span style={s.logo}>Blocks</span>
      </header>

      <div style={s.content}>
        <div style={s.titleRow}>
          <div style={{ display: 'flex', alignItems: 'baseline', gap: 10 }}>
            <h1 style={s.h1}>Mined blocks</h1>
            {total != null && <span style={s.count}>{total.toLocaleString()} total</span>}
          </div>
          {/* Per-page selector sits on the title line (right); wraps below on narrow screens. */}
          <div style={s.sizeRow}>
            <span style={{ fontSize: 12, color: C.muted }}>Per page</span>
            {PAGE_SIZES.map(n => (
              <button key={n} onClick={() => { setPageSize(n); setPage(0) }}
                style={n === pageSize ? { ...s.sizeBtn, ...s.sizeBtnActive } : s.sizeBtn}>
                {n}
              </button>
            ))}
          </div>
        </div>

        {loading && rows.length === 0
          ? <p style={s.dim}>Loading…</p>
          : rows.length === 0
            ? <p style={s.dim}>No blocks mined yet.</p>
            : wide ? <DesktopTable rows={rows} /> : <MobileList rows={rows} />
        }

        <div style={s.navRow}>
          {hasPrev
            ? <button onClick={() => setPage(p => Math.max(0, p - 1))} style={s.navBtn}>← Previous</button>
            : <span style={s.navBtnDisabled}>← Previous</span>}
          <span style={s.pageInfo}>{totalPages != null ? `Page ${page + 1} / ${totalPages}` : `Page ${page + 1}`}</span>
          {hasNext
            ? <button onClick={() => setPage(p => p + 1)} style={s.navBtn}>Next →</button>
            : <span style={s.navBtnDisabled}>Next →</span>}
        </div>
      </div>
    </div>
  )
}

function DesktopTable({ rows }: { rows: BlockRow[] }) {
  return (
    <div style={s.tableWrap}>
      <div style={{ ...s.tr, ...s.thead }}>
        <div style={{ ...s.td, ...s.cBlock }}>Block</div>
        <div style={{ ...s.td, ...s.cTime }}>Time</div>
        <div style={{ ...s.td, ...s.cWinner }}>Winner</div>
        <div style={{ ...s.td, ...s.cReward }}>Reward</div>
        <div style={{ ...s.td, ...s.cCand }}>Online</div>
        <div style={{ ...s.td, ...s.cRand }}>Randomness</div>
      </div>
      {rows.map(b => (
        <a key={b.block_number} href={`/block/${b.block_number}`} style={s.tr}>
          <div style={{ ...s.td, ...s.cBlock, color: C.blue, fontWeight: 700 }}>#{b.block_number}</div>
          <div style={{ ...s.td, ...s.cTime, color: C.muted }}>{b.mined_at ? timeSince(b.mined_at) : '—'}</div>
          <div style={{ ...s.td, ...s.cWinner, ...s.ellip }}>{winnerLabel(b)}</div>
          <div style={{ ...s.td, ...s.cReward, color: C.green, fontWeight: 700 }}>₸{b.reward_tusd}</div>
          <div style={{ ...s.td, ...s.cCand, color: C.muted }}>{b.candidates_count ?? '—'}</div>
          <div style={{ ...s.td, ...s.cRand, ...s.ellip, color: C.muted, fontFamily: 'monospace' }}>{shortHash(b.randomness_source)}</div>
        </a>
      ))}
    </div>
  )
}

function MobileList({ rows }: { rows: BlockRow[] }) {
  return (
    <div>
      {rows.map(b => (
        <a key={b.block_number} href={`/block/${b.block_number}`} style={s.mCard}>
          <div style={s.mTop}>
            <span style={{ color: C.blue, fontWeight: 700, fontSize: 15 }}>#{b.block_number}</span>
            <span style={{ color: C.green, fontWeight: 700, fontSize: 14 }}>₸{b.reward_tusd}</span>
          </div>
          <div style={{ ...s.ellip, color: C.text, fontSize: 13, marginTop: 4 }}>{winnerLabel(b)}</div>
          <div style={s.mMeta}>
            <span>{b.mined_at ? timeSince(b.mined_at) : '—'}</span>
            <span>· {b.candidates_count ?? '—'} online</span>
            <span style={{ fontFamily: 'monospace' }}>· {shortHash(b.randomness_source)}</span>
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

  sizeRow:       { display: 'flex', alignItems: 'center', gap: 6 },
  sizeBtn:       { padding: '5px 12px', background: C.surface, border: `1px solid ${C.border}`, borderRadius: 16, color: C.muted, fontSize: 12, fontWeight: 600, cursor: 'pointer', fontFamily: 'inherit' },
  sizeBtnActive: { background: C.green, color: '#000', borderColor: C.green },

  // Table (desktop): flex rows with fixed-ish columns; long cells ellipsise.
  tableWrap: { background: C.card, border: `1px solid ${C.border}`, borderRadius: 12, overflow: 'hidden', marginBottom: 4 },
  tr:        { display: 'flex', alignItems: 'center', gap: 8, padding: '10px 12px', borderBottom: `1px solid ${C.border}`, textDecoration: 'none', color: C.text, fontSize: 13 },
  thead:     { color: C.muted, fontSize: 11, textTransform: 'uppercase', letterSpacing: 0.5, fontWeight: 700 },
  td:        { minWidth: 0 },
  ellip:     { overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' },
  cBlock:    { flex: '0 0 64px' },
  cTime:     { flex: '0 0 78px' },
  cWinner:   { flex: '1 1 110px' },
  cReward:   { flex: '0 0 70px' },
  cCand:     { flex: '0 0 52px', textAlign: 'center' },
  cRand:     { flex: '0 0 128px' },

  // Cards (mobile)
  mCard: { display: 'block', background: C.card, border: `1px solid ${C.border}`, borderRadius: 10, padding: '11px 14px', marginBottom: 8, textDecoration: 'none', color: C.text },
  mTop:  { display: 'flex', justifyContent: 'space-between', alignItems: 'center' },
  mMeta: { display: 'flex', flexWrap: 'wrap', gap: 6, fontSize: 11, color: C.muted, marginTop: 6 },

  // Pagination — same neutral dark-gray buttons as the block page prev/next.
  navRow:         { display: 'flex', justifyContent: 'space-between', alignItems: 'center', gap: 12, marginTop: 18 },
  navBtn:         { padding: '11px 20px', background: C.surface, border: `1px solid ${C.border}`, borderRadius: 8, color: C.text, fontSize: 14, fontWeight: 700, cursor: 'pointer', fontFamily: 'inherit' },
  navBtnDisabled: { padding: '11px 20px', background: C.card, border: `1px solid ${C.border}`, borderRadius: 8, color: C.muted, fontSize: 14, fontWeight: 700, opacity: 0.6 },
  pageInfo:       { fontSize: 13, color: C.muted },
}

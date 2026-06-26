'use client'

// app/block/[blockNumber]/page.tsx — network.turbousd.com/block/142
//
// Block explorer: shows full details for one mined block.
// Etherscan-style: timestamp, winner, randomness source, candidate count.

import { useEffect, useState } from 'react'
import { supabase } from '@/lib/supabase'

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

interface BlockDetail {
  block_number:       number
  mined_at:           string | null
  reward_tusd:        number
  randomness_source:  string | null
  candidates_count:   number | null
  winner_display_name: string | null
  winner_node_code:   string | null
  winner_is_verified: boolean | null
  winner_is_genesis:  boolean | null
}

function formatTs(iso: string): string {
  const d = new Date(iso)
  return d.toLocaleString('en-GB', {
    day: 'numeric', month: 'short', year: 'numeric',
    hour: '2-digit', minute: '2-digit', timeZone: 'UTC',
  }) + ' UTC'
}

function timeSince(iso: string): string {
  const s = Math.floor((Date.now() - new Date(iso).getTime()) / 1000)
  if (s < 60)    return `${s}s ago`
  if (s < 3600)  return `${Math.floor(s / 60)}m ago`
  if (s < 86400) return `${Math.floor(s / 3600)}h ago`
  return `${Math.floor(s / 86400)}d ago`
}

export default function BlockPage({ params }: { params: { blockNumber: string } }) {
  const blockNum = parseInt(params.blockNumber, 10)
  const [block,   setBlock]   = useState<BlockDetail | null>(null)
  const [loading, setLoading] = useState(true)
  const [prevNum, setPrevNum] = useState<number | null>(null)
  const [nextNum, setNextNum] = useState<number | null>(null)

  useEffect(() => {
    if (isNaN(blockNum)) { setLoading(false); return }

    supabase
      .from('public_mining_feed')
      .select('*')
      .eq('block_number', blockNum)
      .maybeSingle()
      .then(({ data }) => {
        setBlock(data as BlockDetail | null)
        setLoading(false)
      })

    // Fetch adjacent block numbers for prev/next navigation
    Promise.all([
      supabase.from('mining_blocks').select('block_number').eq('block_number', blockNum - 1).maybeSingle(),
      supabase.from('mining_blocks').select('block_number').eq('block_number', blockNum + 1).maybeSingle(),
    ]).then(([prev, next]) => {
      setPrevNum(prev.data?.block_number ?? null)
      setNextNum(next.data?.block_number ?? null)
    })
  }, [blockNum])

  return (
    <div style={s.root}>
      {/* Header */}
      <header style={s.header}>
        <a href="/" style={s.back}>← Network</a>
        <span style={s.logo}>Block #{isNaN(blockNum) ? '?' : blockNum}</span>
        <div style={{ width: 100 }} />
      </header>

      <div style={s.content}>
        {loading ? (
          <p style={s.dim}>Loading…</p>
        ) : !block ? (
          <div style={{ textAlign: 'center', padding: '60px 20px' }}>
            <p style={s.dim}>Block #{blockNum} not found or not yet mined.</p>
            <a href="/network" style={s.btn}>← Back to network</a>
          </div>
        ) : block.mined_at === null ? (
          <div style={{ ...s.card, textAlign: 'center', padding: 40 }}>
            <div style={{ fontSize: 28, marginBottom: 12 }}>⏳</div>
            <div style={{ fontSize: 18, fontWeight: 'bold', color: C.yellow, marginBottom: 8 }}>
              Block #{blockNum} — Mining in progress
            </div>
            <p style={s.dim}>This block hasn't been mined yet. Check back soon.</p>
          </div>
        ) : (
          <>
            {/* Status badge */}
            <div style={s.statusRow}>
              <span style={s.minedBadge}>⛏ Mined</span>
              <span style={s.dim}>{timeSince(block.mined_at)}</span>
            </div>

            {/* Main grid */}
            <div style={s.card}>
              <Row label="Block"        value={`#${block.block_number}`} mono />
              <Row label="Timestamp"    value={formatTs(block.mined_at)} />
              <Row label="Reward"       value={`${block.reward_tusd} ₸USD`} color={C.green} />
              <Row label="Candidates"   value={block.candidates_count != null ? `${block.candidates_count} node${block.candidates_count !== 1 ? 's' : ''} online` : '—'} />
              <Row
                label="Winner"
                value={
                  block.winner_node_code ? (
                    <span style={{ display: 'flex', alignItems: 'center', gap: 8, flexWrap: 'wrap' }}>
                      <a href={`/setup/${block.winner_node_code}`} style={s.link}>
                        {block.winner_display_name || block.winner_node_code}
                      </a>
                      {block.winner_is_verified && <span style={s.verBadge}>✓ verified</span>}
                      {block.winner_is_genesis   && <span style={s.genBadge}>🎖 genesis</span>}
                    </span>
                  ) : '—'
                }
              />
              <Row
                label="Randomness"
                value={
                  block.randomness_source ? (
                    <a
                      href={`https://basescan.org/block/${block.randomness_source}`}
                      target="_blank" rel="noreferrer"
                      style={s.hashLink}
                    >
                      {block.randomness_source.slice(0, 18)}…{block.randomness_source.slice(-6)}
                    </a>
                  ) : '—'
                }
                hint="Base blockchain block hash used as public randomness source"
              />
            </div>

            {/* How the winner was chosen */}
            <div style={s.infoBox}>
              <div style={{ fontSize: 11, fontWeight: 'bold', color: C.muted, marginBottom: 6, textTransform: 'uppercase', letterSpacing: 1 }}>
                How the winner was chosen
              </div>
              <p style={{ fontSize: 13, color: C.muted, lineHeight: 1.7, margin: 0 }}>
                Every online node had an equal chance. The Base block hash above was hashed to an index, and that index selected the winner from the candidate list.{' '}
                Anyone can verify: <code style={s.code}>BigInt("{block.randomness_source}") % {block.candidates_count ?? 'N'}</code> = winner index.
              </p>
            </div>

            {/* Prev / Next navigation */}
            <div style={s.navRow}>
              {prevNum != null
                ? <a href={`/block/${prevNum}`} style={s.navBtn}>← Block #{prevNum}</a>
                : <span style={s.navBtnDisabled}>← Previous</span>
              }
              {nextNum != null
                ? <a href={`/block/${nextNum}`} style={s.navBtn}>Block #{nextNum} →</a>
                : <span style={s.navBtnDisabled}>Next →</span>
              }
            </div>
          </>
        )}
      </div>
    </div>
  )
}

// ── Sub-components ─────────────────────────────────────────────────────────────

function Row({ label, value, mono, color, hint }: {
  label: string
  value: React.ReactNode
  mono?: boolean
  color?: string
  hint?: string
}) {
  return (
    <div style={s.row}>
      <div style={s.rowLabel}>{label}</div>
      <div style={{ ...s.rowValue, fontFamily: mono ? 'monospace' : undefined, color: color ?? C.text }}>
        {value}
        {hint && <div style={{ fontSize: 11, color: C.muted, marginTop: 3, fontFamily: 'system-ui' }}>{hint}</div>}
      </div>
    </div>
  )
}

// ── Styles ─────────────────────────────────────────────────────────────────────

const s: Record<string, React.CSSProperties> = {
  root:    { minHeight: '100vh', background: C.bg, color: C.text, fontFamily: 'system-ui, -apple-system, sans-serif' },
  content: { maxWidth: 640, margin: '0 auto', padding: '28px 20px 80px' },

  header: {
    display: 'flex', alignItems: 'center', justifyContent: 'space-between',
    padding: '0 20px', height: 56, borderBottom: `1px solid ${C.border}`,
    position: 'sticky', top: 0, background: 'rgba(0,0,0,0.92)',
    backdropFilter: 'blur(12px)', zIndex: 10,
  },
  back: { color: C.muted, textDecoration: 'none', fontSize: 14 },
  logo: { fontSize: 16, fontWeight: 'bold', color: C.text },

  statusRow:   { display: 'flex', alignItems: 'center', gap: 10, marginBottom: 16 },
  minedBadge:  { background: `${C.green}18`, border: `1px solid ${C.green}40`, color: C.green, fontSize: 12, fontWeight: 'bold', padding: '4px 10px', borderRadius: 20 },

  card: {
    background: C.card, border: `1px solid ${C.border}`, borderRadius: 12,
    overflow: 'hidden', marginBottom: 16,
  },
  row: {
    display: 'flex', alignItems: 'flex-start', gap: 16,
    padding: '14px 18px', borderBottom: `1px solid ${C.border}`,
  },
  rowLabel: { width: 110, flexShrink: 0, fontSize: 12, color: C.muted, paddingTop: 1 },
  rowValue: { flex: 1, fontSize: 14, lineHeight: 1.5, wordBreak: 'break-all' },

  infoBox: {
    background: `${C.blue}08`, border: `1px solid ${C.blue}20`, borderRadius: 10,
    padding: '14px 16px', marginBottom: 24,
  },

  verBadge: { fontSize: 11, color: C.blue, fontWeight: 'bold' },
  genBadge: { fontSize: 11, color: C.yellow, fontWeight: 'bold' },

  link:     { color: C.green, textDecoration: 'none', fontWeight: 600 },
  hashLink: { color: C.blue, textDecoration: 'none', fontFamily: 'monospace', fontSize: 13 },
  code:     { background: C.surface, border: `1px solid ${C.border}`, borderRadius: 4, padding: '2px 5px', fontFamily: 'monospace', fontSize: 12 },

  dim: { color: C.muted, fontSize: 14 },
  btn: { display: 'inline-block', padding: '10px 20px', border: `1px solid ${C.border}`, borderRadius: 8, color: C.text, textDecoration: 'none', fontSize: 14, marginTop: 16 },

  navRow:         { display: 'flex', justifyContent: 'space-between', gap: 12 },
  navBtn:         { padding: '10px 18px', background: C.surface, border: `1px solid ${C.border}`, borderRadius: 8, color: C.text, textDecoration: 'none', fontSize: 13, fontWeight: 600 },
  navBtnDisabled: { padding: '10px 18px', background: 'transparent', border: `1px solid ${C.border}`, borderRadius: 8, color: C.muted, fontSize: 13, opacity: 0.4 },
}

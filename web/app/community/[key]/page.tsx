'use client'

// app/community/[key]/page.tsx — network.turbousd.com/community/token:base:0x…
//
// Community explorer: one page per project (a token or an NFT collection that
// nodes added in their settings). Shows the community's aggregate mining stats
// and a table of every member node — blocks mined, ₸ earned, uptime — in the
// same table-collapses-to-cards style as the blocks list. Linked from the
// "Part of" tags on block pages and from the "By Communities" leaderboard.

import { useEffect, useState } from 'react'
import { supabase } from '@/lib/supabase'

const C = {
  green:   '#43e397',
  blue:    '#5b8dee',
  yellow:  '#ffcf72',
  bg:      '#000000',
  card:    '#0c0c0c',
  surface: '#141414',
  border:  '#1c1c1c',
  text:    '#e8e8e8',
  muted:   '#6e7280',
}

interface CommunityRow {
  project_key:       string
  kind:              'token' | 'nft'
  name:              string
  symbol:            string | null
  image_url:         string | null
  chain:             string | null
  members_count:     number
  members_online:    number
  blocks_won:        number
  total_tusd_earned: number
}

interface MemberProject {
  node_code:   string
  is_favorite: boolean
  ref_url:     string | null
}

interface DirectoryRow {
  node_code:         string
  display_name:      string | null
  is_verified:       boolean
  is_online:         boolean
  total_tusd_earned: number
  blocks_won:        number
  uptime_seconds:    number | null
  total_uptime_seconds: number | null
  country:           string | null
}

interface MemberRow extends DirectoryRow {
  is_favorite: boolean
}

function fmtUptimeSecs(secs: number): string {
  if (secs <= 0) return '—'
  if (secs < 60) return `${secs}s`
  if (secs < 3600) return `${Math.floor(secs / 60)}m`
  if (secs < 86400) return `${Math.floor(secs / 3600)}h ${Math.floor((secs % 3600) / 60)}m`
  return `${Math.floor(secs / 86400)}d ${Math.floor((secs % 86400) / 3600)}h`
}

export default function CommunityPage({ params }: { params: { key: string } }) {
  const projectKey = decodeURIComponent(params.key)
  const [community, setCommunity] = useState<CommunityRow | null>(null)
  const [members,   setMembers]   = useState<MemberRow[]>([])
  const [refUrl,    setRefUrl]    = useState<string | null>(null)
  const [loading,   setLoading]   = useState(true)
  const [winW,      setWinW]      = useState(1024)

  useEffect(() => {
    const on = () => setWinW(window.innerWidth)
    on(); window.addEventListener('resize', on)
    return () => window.removeEventListener('resize', on)
  }, [])

  useEffect(() => {
    let cancelled = false
    async function load() {
      setLoading(true)
      const [{ data: comm }, { data: projs }] = await Promise.all([
        supabase.from('public_community_leaderboard').select('*').eq('project_key', projectKey).maybeSingle(),
        supabase.from('public_node_projects').select('node_code, is_favorite, ref_url').eq('project_key', projectKey),
      ])
      if (cancelled) return
      setCommunity((comm ?? null) as CommunityRow | null)
      const memberProjects = (projs ?? []) as MemberProject[]
      setRefUrl(memberProjects.find(p => p.ref_url)?.ref_url ?? null)
      if (memberProjects.length > 0) {
        const { data: dirs } = await supabase
          .from('public_node_directory')
          .select('node_code, display_name, is_verified, is_online, total_tusd_earned, blocks_won, uptime_seconds, total_uptime_seconds, country')
          .in('node_code', memberProjects.map(p => p.node_code))
        if (cancelled) return
        const favSet = new Set(memberProjects.filter(p => p.is_favorite).map(p => p.node_code))
        const rows = ((dirs ?? []) as DirectoryRow[])
          .map(d => ({ ...d, is_favorite: favSet.has(d.node_code) }))
          .sort((a, b) => b.blocks_won - a.blocks_won || b.total_tusd_earned - a.total_tusd_earned)
        setMembers(rows)
      } else {
        setMembers([])
      }
      setLoading(false)
    }
    load()
    return () => { cancelled = true }
  }, [projectKey])

  const wide = winW >= 640
  const title = community?.name ?? 'Community'

  return (
    <div style={s.root}>
      <header style={s.header}>
        <a href="/" style={s.back}>← Network</a>
        <span style={s.logo}>Community</span>
        <div style={{ width: 72 }} />
      </header>

      <div style={s.content}>
        {loading ? (
          <p style={s.dim}>Loading…</p>
        ) : !community && members.length === 0 ? (
          <div style={{ textAlign: 'center', padding: '60px 20px' }}>
            <p style={s.dim}>No nodes have added this community yet.</p>
            <a href="/" style={s.btn}>← Back to network</a>
          </div>
        ) : (
          <>
            {/* ── Community hero ── */}
            <div style={s.hero}>
              {community?.image_url && (
                // eslint-disable-next-line @next/next/no-img-element
                <img src={community.image_url} alt="" style={s.heroImg} />
              )}
              <div style={{ flex: 1, minWidth: 0 }}>
                <h1 style={s.h1}>
                  {title}
                  {community?.symbol && <span style={{ color: C.muted, fontWeight: 400, fontSize: 16, marginLeft: 8 }}>{community.symbol}</span>}
                </h1>
                <div style={{ fontSize: 12, color: C.muted, marginTop: 4 }}>
                  {community?.kind === 'nft' ? 'NFT collection' : 'Token'}
                  {community?.chain ? ` · ${community.chain}` : ''}
                  {refUrl && (
                    <>
                      {' · '}
                      <a href={refUrl} target="_blank" rel="noreferrer" style={{ color: C.green, textDecoration: 'none' }}>
                        View project ↗
                      </a>
                    </>
                  )}
                </div>
              </div>
            </div>

            {/* ── Aggregate stats ── */}
            <div style={s.statsBar}>
              <StatPill label="Members"      value={String(community?.members_count ?? members.length)} color={C.text} />
              <StatPill label="Online now"   value={String(community?.members_online ?? members.filter(m => m.is_online).length)} color={C.green} />
              <StatPill label="Blocks mined" value={String(community?.blocks_won ?? members.reduce((a, m) => a + m.blocks_won, 0))} color={C.yellow} />
              <StatPill label="₸ earned"     value={`₸${(community?.total_tusd_earned ?? members.reduce((a, m) => a + m.total_tusd_earned, 0)).toFixed(1)}`} color={C.green} />
            </div>

            {/* ── Members table ── */}
            <h2 style={s.sectionTitle}>Members</h2>
            {members.length === 0
              ? <p style={s.dim}>No members yet.</p>
              : wide ? <DesktopTable rows={members} /> : <MobileList rows={members} />}
            <p style={{ fontSize: 11, color: C.muted, marginTop: 12, opacity: 0.7 }}>
              ★ = nodes that picked this community as their favorite (shown on their mined blocks).
            </p>
          </>
        )}
      </div>
    </div>
  )
}

// ── Sub-components ─────────────────────────────────────────────────────────────

function StatPill({ label, value, color }: { label: string; value: string; color: string }) {
  return (
    <div style={s.statPill}>
      <div style={{ fontSize: 18, fontWeight: 'bold', color }}>{value}</div>
      <div style={{ fontSize: 10, color: C.muted, marginTop: 3, textTransform: 'uppercase', letterSpacing: 0.8 }}>{label}</div>
    </div>
  )
}

function nameLabel(m: MemberRow): string {
  return m.display_name || `Node #${m.node_code}`
}

function DesktopTable({ rows }: { rows: MemberRow[] }) {
  return (
    <div style={s.tableWrap}>
      <div style={{ ...s.tr, ...s.thead }}>
        <div style={{ ...s.td, ...s.cRank }}>#</div>
        <div style={{ ...s.td, ...s.cName }}>Node</div>
        <div style={{ ...s.td, ...s.cBlocks }}>Blocks</div>
        <div style={{ ...s.td, ...s.cEarned }}>Earned</div>
        <div style={{ ...s.td, ...s.cUptime }}>Uptime</div>
      </div>
      {rows.map((m, idx) => (
        <a key={m.node_code} href={`/node/${m.node_code}`} style={s.tr}>
          <div style={{ ...s.td, ...s.cRank, color: C.muted }}>{idx + 1}</div>
          <div style={{ ...s.td, ...s.cName, display: 'flex', alignItems: 'center', gap: 7, minWidth: 0 }}>
            <span style={{
              width: 6, height: 6, borderRadius: '50%', flexShrink: 0,
              background: m.is_online ? C.green : '#2a2a2a',
              boxShadow: m.is_online ? `0 0 5px ${C.green}88` : 'none',
            }} />
            <span style={{ ...s.ellip, fontWeight: 600 }}>{nameLabel(m)}</span>
            {m.is_verified && <span style={{ fontSize: 10, color: '#1d9bf0', flexShrink: 0 }}>✓</span>}
            {m.is_favorite && <span style={{ fontSize: 10, color: C.yellow, flexShrink: 0 }} title="Favorite community of this node">★</span>}
            {m.country && <span style={{ ...s.ellip, fontSize: 11, color: C.muted }}>{m.country}</span>}
          </div>
          <div style={{ ...s.td, ...s.cBlocks, color: m.blocks_won > 0 ? C.yellow : C.muted, fontWeight: 700 }}>{m.blocks_won}</div>
          <div style={{ ...s.td, ...s.cEarned, color: m.total_tusd_earned > 0 ? C.green : C.muted, fontWeight: 700 }}>₸{m.total_tusd_earned.toFixed(1)}</div>
          <div style={{ ...s.td, ...s.cUptime, color: C.muted }}>{fmtUptimeSecs(m.total_uptime_seconds ?? m.uptime_seconds ?? 0)}</div>
        </a>
      ))}
    </div>
  )
}

function MobileList({ rows }: { rows: MemberRow[] }) {
  return (
    <div>
      {rows.map((m, idx) => (
        <a key={m.node_code} href={`/node/${m.node_code}`} style={s.mCard}>
          <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
            <span style={{ color: C.muted, fontSize: 12, width: 20, flexShrink: 0 }}>{idx + 1}</span>
            <span style={{
              width: 6, height: 6, borderRadius: '50%', flexShrink: 0,
              background: m.is_online ? C.green : '#2a2a2a',
            }} />
            <span style={{ ...s.ellip, fontWeight: 600, fontSize: 14, flex: 1, minWidth: 0 }}>{nameLabel(m)}</span>
            {m.is_favorite && <span style={{ fontSize: 11, color: C.yellow, flexShrink: 0 }}>★</span>}
            <span style={{ color: m.blocks_won > 0 ? C.yellow : C.muted, fontWeight: 700, fontSize: 13, flexShrink: 0 }}>{m.blocks_won} blk</span>
          </div>
          <div style={s.mMeta}>
            {m.country && <span>{m.country}</span>}
            <span>· ₸{m.total_tusd_earned.toFixed(1)}</span>
            <span>· {fmtUptimeSecs(m.total_uptime_seconds ?? m.uptime_seconds ?? 0)} uptime</span>
          </div>
        </a>
      ))}
    </div>
  )
}

// ── Styles ─────────────────────────────────────────────────────────────────────

const s: Record<string, React.CSSProperties> = {
  root:    { minHeight: '100vh', background: C.bg, color: C.text, fontFamily: 'system-ui, -apple-system, sans-serif' },
  header:  {
    display: 'flex', alignItems: 'center', justifyContent: 'space-between',
    padding: '0 20px', height: 56, borderBottom: `1px solid ${C.border}`,
    position: 'sticky', top: 0, background: 'rgba(0,0,0,0.92)', backdropFilter: 'blur(12px)', zIndex: 10,
  },
  back:    { color: C.muted, textDecoration: 'none', fontSize: 14 },
  logo:    { fontSize: 16, fontWeight: 'bold', color: C.text },
  content: { maxWidth: 800, margin: '0 auto', padding: '24px 16px 80px' },

  hero:    { display: 'flex', alignItems: 'center', gap: 14, marginBottom: 16 },
  heroImg: { width: 52, height: 52, borderRadius: 12, objectFit: 'cover', background: '#111', border: `1px solid ${C.border}`, flexShrink: 0 },
  h1:      { fontSize: 22, margin: 0, display: 'flex', alignItems: 'baseline', flexWrap: 'wrap' },

  statsBar: { display: 'flex', gap: 8, flexWrap: 'wrap', margin: '4px 0 26px' },
  statPill: {
    background: C.card, border: `1px solid ${C.border}`, borderRadius: 12,
    padding: '10px 8px', textAlign: 'center', flex: '1 1 0', minWidth: 100,
  },

  sectionTitle: { fontSize: 10, fontWeight: 'bold', color: C.muted, textTransform: 'uppercase', letterSpacing: 1.4, marginBottom: 12 },

  // Table (desktop) — same pattern as /blocks
  tableWrap: { background: C.card, border: `1px solid ${C.border}`, borderRadius: 12, overflow: 'hidden', marginBottom: 4 },
  tr:        { display: 'flex', alignItems: 'center', gap: 8, padding: '10px 12px', borderBottom: `1px solid ${C.border}`, textDecoration: 'none', color: C.text, fontSize: 13 },
  thead:     { color: C.muted, fontSize: 11, textTransform: 'uppercase', letterSpacing: 0.5, fontWeight: 700 },
  td:        { minWidth: 0 },
  ellip:     { overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' },
  cRank:     { flex: '0 0 24px', textAlign: 'center' },
  cName:     { flex: '1 1 160px' },
  cBlocks:   { flex: '0 0 56px', textAlign: 'right' },
  cEarned:   { flex: '0 0 76px', textAlign: 'right' },
  cUptime:   { flex: '0 0 80px', textAlign: 'right' },

  // Cards (mobile)
  mCard: { display: 'block', background: C.card, border: `1px solid ${C.border}`, borderRadius: 10, padding: '11px 14px', marginBottom: 8, textDecoration: 'none', color: C.text },
  mMeta: { display: 'flex', flexWrap: 'wrap', gap: 6, fontSize: 11, color: C.muted, marginTop: 6, paddingLeft: 28 },

  dim: { color: C.muted, fontSize: 14 },
  btn: { display: 'inline-block', padding: '10px 20px', border: `1px solid ${C.border}`, borderRadius: 8, color: C.text, textDecoration: 'none', fontSize: 14, marginTop: 16 },
}

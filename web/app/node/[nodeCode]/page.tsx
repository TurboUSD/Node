// app/node/[nodeCode]/page.tsx, PUBLIC, read-only node profile.
//
// This is what the on-device QR code and the network dashboard's node-name /
// "View profile" links point to. It shows the owner-set identity (name, bio,
// location, X handle) plus live network stats (uptime, rewards, blocks, join
// date). It is deliberately NOT the /setup/[nodeId] page, which is the private
// owner-only config screen and 404s ("No node found") for visitors.
'use client'

import { useEffect, useState } from 'react'
import { useParams } from 'next/navigation'
import { supabase } from '@/lib/supabase'
import { VerifiedBadge, UnverifiedBadge, GenesisBadge, LocationNote } from '@/components/NodeBadges'

const C = {
  bg:     '#000000',   // match the global body (#000) — the 560px centered column
                       // used to be #0a0a0a, so it showed as a lighter strip
  card:   '#141416',   // was #111
  border: '#2a2a2e',
  text:   '#e8e8ea',
  muted:  '#9096a1',   // was #6e7280 — secondary text was too dark
  green:  '#43e397',
  yellow: '#ffcf72',
  blue:   '#5b8dee',
  surface:'#1b1b1e',   // stat-card fill, matches the overlay card
  statVal:'#d2d2d8',   // unified stat value colour, matches the overlay card
}

interface NodeProfile {
  node_code:      string
  display_name:   string | null
  bio:            string | null
  twitter_handle: string | null
  country:        string | null
  city:           string | null
  is_verified:    boolean
  is_genesis:     boolean
  created_at:     string
}

interface NodeStats {
  uptime_pct:            number
  total_uptime_seconds:  number | null
  uptime_seconds:        number | null
  blocks_won:            number
  total_tusd_earned:     number
  is_online:             boolean
  last_seen_at:          string | null
}

function fmtUptime(secs: number): string {
  if (secs <= 0) return '—'
  if (secs < 60)    return `${secs}s`
  if (secs < 3600)  return `${Math.floor(secs / 60)}m`
  if (secs < 86400) return `${Math.floor(secs / 3600)}h ${Math.floor((secs % 3600) / 60)}m`
  return `${Math.floor(secs / 86400)}d ${Math.floor((secs % 86400) / 3600)}h`
}

function timeSince(iso: string): string {
  const sec = Math.floor((Date.now() - new Date(iso).getTime()) / 1000)
  if (sec < 60)    return `${sec}s ago`
  if (sec < 3600)  return `${Math.floor(sec / 60)}m ago`
  if (sec < 86400) return `${Math.floor(sec / 3600)}h ago`
  const d = Math.floor(sec / 86400)
  return d === 1 ? '1 day ago' : `${d} days ago`
}

function joinDate(iso: string): string {
  return new Date(iso).toLocaleDateString('en-GB', { day: '2-digit', month: 'short', year: 'numeric' })
}

export default function PublicNodePage() {
  const params = useParams()
  const nodeCode = String(params?.nodeCode ?? '').toUpperCase()

  const [node,  setNode]  = useState<NodeProfile | null>(null)
  const [stats, setStats] = useState<NodeStats | null>(null)
  const [lastBlock, setLastBlock] = useState<{ block_number: number; mined_at: string } | null>(null)
  const [prevCode, setPrevCode] = useState<string | null>(null)   // neighbours in registration order
  const [nextCode, setNextCode] = useState<string | null>(null)
  const [loading, setLoading] = useState(true)

  useEffect(() => {
    if (!nodeCode) return
    supabase
      .from('nodes')
      .select('node_code, display_name, bio, twitter_handle, country, city, is_verified, is_genesis, created_at')
      .eq('node_code', nodeCode)
      .maybeSingle()
      .then(({ data }) => {
        if (data) setNode(data as NodeProfile)
        setLoading(false)
      })
    supabase
      .from('public_node_directory')
      .select('uptime_pct, total_uptime_seconds, uptime_seconds, blocks_won, total_tusd_earned, is_online, last_seen_at')
      .eq('node_code', nodeCode)
      .maybeSingle()
      .then(({ data }) => { if (data) setStats(data as NodeStats) })
    supabase
      .from('public_mining_feed')
      .select('block_number, mined_at')
      .eq('winner_node_code', nodeCode)
      .not('mined_at', 'is', null)
      .order('block_number', { ascending: false })
      .limit(1)
      .maybeSingle()
      .then(({ data }) => { if (data) setLastBlock(data as { block_number: number; mined_at: string }) })
  }, [nodeCode])

  // Neighbours for the Prev/Next buttons — in registration order (join date), so
  // "Previous" is the node registered just before this one and "Next" just after,
  // matching the default order of the /nodes list.
  useEffect(() => {
    if (!node?.created_at) return
    supabase
      .from('public_node_directory')
      .select('node_code')
      .lt('created_at', node.created_at)
      .order('created_at', { ascending: false })
      .limit(1)
      .maybeSingle()
      .then(({ data }) => setPrevCode((data as { node_code: string } | null)?.node_code ?? null))
    supabase
      .from('public_node_directory')
      .select('node_code')
      .gt('created_at', node.created_at)
      .order('created_at', { ascending: true })
      .limit(1)
      .maybeSingle()
      .then(({ data }) => setNextCode((data as { node_code: string } | null)?.node_code ?? null))
  }, [node?.created_at])

  if (loading) {
    return <main style={s.page}><p style={{ color: C.muted, textAlign: 'center', marginTop: 80 }}>Loading node {nodeCode}…</p></main>
  }

  if (!node) {
    return (
      <main style={s.page}>
        <div style={{ ...s.card, textAlign: 'center', marginTop: 60 }}>
          <div style={{ fontSize: 40, marginBottom: 10 }}>🔌</div>
          <h1 style={{ fontSize: 20, margin: '0 0 8px' }}>Node {nodeCode} not found</h1>
          <p style={{ color: C.muted, fontSize: 14, margin: '0 0 20px' }}>
            This node isn’t registered on the TurboUSD network yet. If you just set it up, give it a minute to check in.
          </p>
          <a href="/" style={s.linkBtn}>← Back to the network</a>
        </div>
      </main>
    )
  }

  const name = node.display_name || `Node ${node.node_code}`
  const totalUptime = stats?.total_uptime_seconds ?? stats?.uptime_seconds ?? 0
  const location = [node.city, node.country].filter(Boolean).join(', ')

  return (
    <main style={s.page}>
      {/* Soft "alive" pulse for the online dot (matches the device footer dot). */}
      <style>{`@keyframes tgNodePulse{0%,100%{opacity:1}50%{opacity:.5}}`}</style>
      <header style={s.header}>
        <a href="/" style={s.back}>← Network</a>
        <span style={{ fontSize: 13, color: C.muted }}>Node {node.node_code}</span>
      </header>

      <div style={s.card}>
        {/* Line 1: online dot (soft pulse) + name + verified/unverified + genesis */}
        <div style={{ display: 'flex', alignItems: 'center', gap: 10, marginBottom: 6, flexWrap: 'wrap' }}>
          <span style={{
            width: 10, height: 10, borderRadius: '50%',
            background: stats?.is_online ? C.green : '#333',
            boxShadow: stats?.is_online ? `0 0 8px ${C.green}88` : 'none',
            animation: stats?.is_online ? 'tgNodePulse 2.4s ease-in-out infinite' : undefined,
            flexShrink: 0,
          }} />
          <h1 style={{ fontSize: 24, margin: 0 }}>{name}</h1>
          {node.is_verified ? <VerifiedBadge /> : <UnverifiedBadge />}
          {node.is_genesis  && <GenesisBadge />}
        </div>

        {/* Line 2: id · country + info · X handle (inline text, " · " separators). */}
        {(() => {
          const parts: React.ReactNode[] = []
          if (node.display_name) parts.push(<span key="id">#{node.node_code}</span>)
          if (location) parts.push(<span key="loc">{location}<LocationNote /></span>)
          if (node.twitter_handle) parts.push(
            <a key="tw" href={`https://x.com/${node.twitter_handle.replace(/^@/, '')}`} target="_blank" rel="noreferrer"
              style={{ color: C.green, textDecoration: 'none' }}>@{node.twitter_handle.replace(/^@/, '')}</a>,
          )
          return parts.length > 0 ? (
            <div style={{ color: C.muted, fontSize: 13 }}>
              {parts.map((p, i) => <span key={i}>{i > 0 && ' · '}{p}</span>)}
            </div>
          ) : null
        })()}

        {/* Line 3: live status — Online now if online, else last seen. */}
        <div style={{ color: C.muted, fontSize: 13, marginTop: 3, marginBottom: node.bio ? 14 : 18 }}>
          {stats?.is_online ? 'Online now' : stats?.last_seen_at ? `Last seen ${timeSince(stats.last_seen_at)}` : 'Offline'}
        </div>

        {node.bio && <p style={{ color: C.text, fontSize: 15, lineHeight: 1.7, margin: '0 0 20px' }}>{node.bio}</p>}

        {/* Same four cards, order and colours as the overlay card. */}
        <div style={s.statsGrid}>
          <Stat label="Earned"  value={`₸${(stats?.total_tusd_earned ?? 0).toFixed(2)}`}    color={C.statVal} />
          <Stat label="Blocks"  value={String(stats?.blocks_won ?? 0)}                       color={C.statVal} />
          <Stat label="Uptime"  value={fmtUptime(totalUptime)}                               color={C.statVal} />
          <Stat label="Since"   value={joinDate(node.created_at)}                            color={C.statVal} />
        </div>

        {/* Last block won — same area the map overlay card shows. */}
        {lastBlock && (
          <div style={s.lastBlockBox}>
            <div style={{ fontSize: 10, color: C.muted, textTransform: 'uppercase', letterSpacing: 0.8, marginBottom: 6 }}>Last block won</div>
            <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between' }}>
              <span style={{ fontSize: 14, color: C.text, fontWeight: 600 }}>Block #{lastBlock.block_number}</span>
              <span style={{ fontSize: 12, color: C.muted }}>{timeSince(lastBlock.mined_at)}</span>
            </div>
            <a href={`/block/${lastBlock.block_number}`} style={{ fontSize: 12, color: C.green, textDecoration: 'none', marginTop: 6, display: 'inline-block' }}>View in explorer →</a>
          </div>
        )}
      </div>

      {/* Navigate between nodes (registration order) + link to the full list. */}
      <div style={s.navRow}>
        {prevCode
          ? <a href={`/node/${prevCode}`} style={s.navBtn}>← Previous</a>
          : <span style={s.navBtnDisabled}>← Previous</span>}
        <a href="/nodes" style={s.navListLink}>☰ Node List</a>
        {nextCode
          ? <a href={`/node/${nextCode}`} style={s.navBtn}>Next →</a>
          : <span style={s.navBtnDisabled}>Next →</span>}
      </div>

      <p style={{ textAlign: 'center', color: C.muted, fontSize: 12, marginTop: 18 }}>
        Part of the <a href="/" style={{ color: C.green, textDecoration: 'none' }}>TurboUSD mining network</a>
      </p>
    </main>
  )
}

function Stat({ label, value, color }: { label: string; value: string; color: string }) {
  // Bordered stat card, same format as the map overlay card (2x2 grid).
  return (
    <div style={{
      background: C.surface, border: `1px solid ${C.border}`, borderRadius: 10, padding: '14px 16px',
      display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'flex-start', minWidth: 0,
    }}>
      <div style={{
        fontSize: 16, fontWeight: 'bold', color, lineHeight: 1.2, minHeight: 22,
        display: 'flex', alignItems: 'center', justifyContent: 'center', textAlign: 'center',
      }}>{value}</div>
      <div style={{ fontSize: 10, color: C.muted, marginTop: 4, textTransform: 'uppercase', letterSpacing: 0.8 }}>{label}</div>
    </div>
  )
}

const s: Record<string, React.CSSProperties> = {
  page:   { maxWidth: 560, margin: '0 auto', padding: 20, minHeight: '100vh', background: C.bg, color: C.text, fontFamily: '-apple-system, system-ui, sans-serif' },
  header: { display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginBottom: 20 },
  back:   { color: C.green, textDecoration: 'none', fontSize: 14 },
  card:   { background: C.card, border: `1px solid ${C.border}`, borderRadius: 14, padding: 22 },
  badge:  { background: C.blue, color: '#fff', borderRadius: '50%', width: 20, height: 20, display: 'inline-flex', alignItems: 'center', justifyContent: 'center', fontSize: 12, fontWeight: 700 },
  statsGrid: { display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 10 },
  lastBlockBox: { marginTop: 14, padding: '12px 14px', background: C.surface, border: `1px solid ${C.border}`, borderRadius: 10 },
  linkBtn: { display: 'inline-block', background: '#1c1c1c', border: `1px solid ${C.border}`, borderRadius: 8, padding: '10px 18px', color: C.text, textDecoration: 'none', fontSize: 14 },

  // Prev / Node List / Next — same neutral dark buttons as the block page.
  navRow:         { display: 'flex', justifyContent: 'space-between', alignItems: 'center', gap: 12, marginTop: 18 },
  navBtn:         { padding: '11px 20px', background: C.surface, border: `1px solid ${C.border}`, borderRadius: 8, color: C.text, textDecoration: 'none', fontSize: 14, fontWeight: 700, display: 'inline-block', cursor: 'pointer' },
  navBtnDisabled: { padding: '11px 20px', background: C.card, border: `1px solid ${C.border}`, borderRadius: 8, color: C.muted, fontSize: 14, fontWeight: 700, opacity: 0.6, display: 'inline-block' },
  navListLink:    { color: C.muted, textDecoration: 'none', fontSize: 13, fontWeight: 600, whiteSpace: 'nowrap', flexShrink: 0 },
}

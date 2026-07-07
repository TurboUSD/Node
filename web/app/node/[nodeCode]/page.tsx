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
import { VerifiedBadge, UnverifiedBadge, GenesisBadge } from '@/components/NodeBadges'

const C = {
  bg:     '#0a0a0a',
  card:   '#141416',   // was #111
  border: '#2a2a2e',
  text:   '#e8e8ea',
  muted:  '#9096a1',   // was #6e7280 — secondary text was too dark
  green:  '#43e397',
  yellow: '#ffcf72',
  blue:   '#5b8dee',
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
  uptime_pct:        number
  blocks_won:        number
  total_tusd_earned: number
  is_online:         boolean
  last_seen_at:      string | null
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
      .select('uptime_pct, blocks_won, total_tusd_earned, is_online, last_seen_at')
      .eq('node_code', nodeCode)
      .maybeSingle()
      .then(({ data }) => { if (data) setStats(data as NodeStats) })
  }, [nodeCode])

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
  const uptime = stats?.uptime_pct ?? 0
  const uptimeColor = uptime >= 90 ? C.green : uptime >= 60 ? C.yellow : C.muted
  const location = [node.city, node.country].filter(Boolean).join(', ')

  return (
    <main style={s.page}>
      <header style={s.header}>
        <a href="/" style={s.back}>← Network</a>
        <span style={{ fontSize: 13, color: C.muted }}>Node {node.node_code}</span>
      </header>

      <div style={s.card}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 10, marginBottom: 6 }}>
          <span style={{
            width: 10, height: 10, borderRadius: '50%',
            background: stats?.is_online ? C.green : '#333',
            boxShadow: stats?.is_online ? `0 0 8px ${C.green}88` : 'none',
            flexShrink: 0,
          }} />
          <h1 style={{ fontSize: 24, margin: 0 }}>{name}</h1>
          {node.is_verified ? <VerifiedBadge /> : <UnverifiedBadge />}
          {node.is_genesis  && <GenesisBadge />}
        </div>

        <div style={{ color: C.muted, fontSize: 13, marginBottom: node.bio ? 14 : 18 }}>
          {stats?.is_online ? 'Online now' : stats?.last_seen_at ? `Last seen ${timeSince(stats.last_seen_at)}` : 'Offline'}
          {location && <> · {location}</>}
          {node.twitter_handle && <> · <a href={`https://x.com/${node.twitter_handle.replace(/^@/, '')}`} style={{ color: C.blue, textDecoration: 'none' }}>@{node.twitter_handle.replace(/^@/, '')}</a></>}
        </div>

        {node.bio && <p style={{ color: C.text, fontSize: 15, lineHeight: 1.7, margin: '0 0 20px' }}>{node.bio}</p>}

        <div style={s.statsGrid}>
          <Stat label="Uptime"  value={`${uptime}%`}                                        color={uptimeColor} />
          <Stat label="Earned"  value={`₸${(stats?.total_tusd_earned ?? 0).toFixed(2)}`}    color={C.green} />
          <Stat label="Blocks"  value={String(stats?.blocks_won ?? 0)}                       color={C.blue} />
          <Stat label="Since"   value={joinDate(node.created_at)}                            color={C.yellow} />
        </div>
      </div>

      <p style={{ textAlign: 'center', color: C.muted, fontSize: 12, marginTop: 18 }}>
        Part of the <a href="/" style={{ color: C.green, textDecoration: 'none' }}>TurboUSD mining network</a>
      </p>
    </main>
  )
}

function Stat({ label, value, color }: { label: string; value: string; color: string }) {
  return (
    <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', textAlign: 'center', minWidth: 0 }}>
      {/* Flexible min-height (fits two lines like "27 Jun 2026") so the value
          never overflows onto the label below, and labels stay aligned across
          all four columns. */}
      <div style={{
        fontSize: 16, fontWeight: 'bold', color, lineHeight: 1.2, minHeight: 40,
        display: 'flex', alignItems: 'center', justifyContent: 'center',
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
  statsGrid: { display: 'grid', gridTemplateColumns: 'repeat(4, 1fr)', gap: 12, alignItems: 'start', borderTop: `1px solid ${C.border}`, paddingTop: 18 },
  linkBtn: { display: 'inline-block', background: '#1c1c1c', border: `1px solid ${C.border}`, borderRadius: 8, padding: '10px 18px', color: C.text, textDecoration: 'none', fontSize: 14 },
}

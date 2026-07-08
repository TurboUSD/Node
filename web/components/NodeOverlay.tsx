'use client'

// components/NodeOverlay.tsx — the node "card" as a superimposed bottom-sheet
// overlay, fetched by node_code from public_node_directory. Used by the block
// explorer (tap the winner's name/id) and reusable anywhere a node reference
// should expand into its full card.

import { useEffect, useState } from 'react'
import { supabase } from '@/lib/supabase'
import { VerifiedBadge, UnverifiedBadge, GenesisBadge, LocationNote } from './NodeBadges'

const C = {
  green: '#43e397', blue: '#5b8dee', yellow: '#ffcf72',
  card: 'rgb(24, 24, 24)', border: '#2f2f33', text: '#e8e8ea',
  muted: '#9096a1',      // was #6e7280 — secondary text was too dark
  statVal: '#d2d2d8',    // unified stat value colour (slightly-muted white)
}

interface NodeRow {
  node_code: string
  display_name: string | null
  bio: string | null
  is_verified: boolean
  is_genesis: boolean
  is_online: boolean
  total_tusd_earned: number
  blocks_won: number
  uptime_seconds: number | null
  total_uptime_seconds: number | null
  created_at: string
  last_seen_at: string | null
  twitter_handle: string | null
  country: string | null
  city: string | null
}

function timeSince(iso: string): string {
  const s = Math.floor((Date.now() - new Date(iso).getTime()) / 1000)
  if (s < 60) return `${s}s ago`
  if (s < 3600) return `${Math.floor(s / 60)}m ago`
  if (s < 86400) return `${Math.floor(s / 3600)}h ago`
  const d = Math.floor(s / 86400)
  return d === 1 ? '1 day ago' : `${d} days ago`
}

function fmtUptime(secs: number): string {
  if (secs <= 0) return '—'
  if (secs < 60) return `${secs}s`
  if (secs < 3600) return `${Math.floor(secs / 60)}m`
  if (secs < 86400) return `${Math.floor(secs / 3600)}h ${Math.floor((secs % 3600) / 60)}m`
  return `${Math.floor(secs / 86400)}d ${Math.floor((secs % 86400) / 3600)}h`
}

function joinDate(iso: string): string {
  return new Date(iso).toLocaleDateString('en-GB', { day: '2-digit', month: 'short', year: 'numeric' })
}

export default function NodeOverlay({ nodeCode, onClose }: { nodeCode: string; onClose: () => void }) {
  const [node, setNode] = useState<NodeRow | null>(null)
  const [loading, setLoading] = useState(true)

  useEffect(() => {
    let alive = true
    supabase.from('public_node_directory').select('*').eq('node_code', nodeCode).maybeSingle()
      .then(({ data }) => { if (alive) { setNode(data as NodeRow | null); setLoading(false) } })
    return () => { alive = false }
  }, [nodeCode])

  const totalUptime = node ? (node.total_uptime_seconds ?? node.uptime_seconds ?? 0) : 0
  const location = node ? [node.city, node.country].filter(Boolean).join(', ') : ''
  const name = node?.display_name || `Node #${nodeCode}`

  function shareOnX() {
    if (!node) return
    const up = totalUptime > 0 ? ` · ${fmtUptime(totalUptime)} uptime` : ''
    const text = `Node "${name}" is live on the @TurboUSD network ⛏\n${node.blocks_won} blocks won · ${node.total_tusd_earned.toFixed(2)} ₸USD earned${up}`
    const url = `https://network.turbousd.com/node/${nodeCode}`
    window.open(`https://x.com/intent/tweet?text=${encodeURIComponent(text)}&url=${encodeURIComponent(url)}`, '_blank')
  }

  return (
    <>
      <div onClick={onClose} style={{ position: 'fixed', inset: 0, background: 'rgba(0,0,0,0.72)', zIndex: 2000 }} />
      <div role="dialog" aria-modal="true" style={{
        position: 'fixed', bottom: 0, left: 0, right: 0, maxWidth: 600, margin: '0 auto',
        background: C.card, border: `1px solid ${C.border}`, borderBottom: 'none',
        borderTopLeftRadius: 20, borderTopRightRadius: 20, padding: '30px 22px calc(40px + env(safe-area-inset-bottom,0px))',
        zIndex: 2001, maxHeight: '82vh', overflowY: 'auto', color: C.text,
        fontFamily: 'system-ui, -apple-system, sans-serif',
      }}>
        <button onClick={onClose} aria-label="Close" style={{
          position: 'absolute', top: 14, right: 16, width: 32, height: 32, borderRadius: '50%',
          background: '#141414', border: `1px solid ${C.border}`, color: C.muted, cursor: 'pointer', fontSize: 14,
        }}>✕</button>

        {loading ? (
          <p style={{ color: C.muted, fontSize: 14, padding: '10px 0' }}>Loading node {nodeCode}…</p>
        ) : !node ? (
          <p style={{ color: C.muted, fontSize: 14, padding: '10px 0' }}>Node {nodeCode} isn&apos;t on the network yet.</p>
        ) : (
          <>
            <div style={{ display: 'flex', alignItems: 'center', gap: 10, marginBottom: 6, flexWrap: 'wrap' }}>
              <span style={{
                width: 10, height: 10, borderRadius: '50%', flexShrink: 0,
                background: node.is_online ? C.green : '#333',
                boxShadow: node.is_online ? `0 0 8px ${C.green}88` : 'none',
              }} />
              <span style={{ fontSize: 22, fontWeight: 'bold' }}>{name}</span>
              {node.is_verified ? <VerifiedBadge size={18} /> : <UnverifiedBadge size={14} />}
              {node.is_genesis && <GenesisBadge size={16} />}
            </div>

            <div style={{ color: C.muted, fontSize: 13, marginBottom: node.bio ? 14 : 18 }}>
              {node.is_online ? 'Online now' : node.last_seen_at ? `Last seen ${timeSince(node.last_seen_at)}` : 'Offline'}
              {location && <> · {location}<LocationNote /></>}
              {node.twitter_handle && <> · <a href={`https://x.com/${node.twitter_handle.replace(/^@/, '')}`} target="_blank" rel="noreferrer" style={{ color: C.blue, textDecoration: 'none' }}>@{node.twitter_handle.replace(/^@/, '')}</a></>}
            </div>

            {node.bio && <p style={{ color: C.text, fontSize: 15, lineHeight: 1.7, margin: '0 0 20px' }}>{node.bio}</p>}

            <div style={{ display: 'grid', gridTemplateColumns: 'repeat(4, 1fr)', gap: 10, borderTop: `1px solid ${C.border}`, paddingTop: 11 }}>
              <Stat label="Earned" value={`₸${node.total_tusd_earned.toFixed(2)}`} color={C.statVal} />
              <Stat label="Blocks" value={String(node.blocks_won)} color={C.statVal} />
              <Stat label="Uptime" value={fmtUptime(totalUptime)} color={C.statVal} />
              <Stat label="Since" value={joinDate(node.created_at)} color={C.statVal} />
            </div>

            <div style={{ display: 'flex', gap: 10, marginTop: 18, flexWrap: 'wrap' }}>
              <button onClick={shareOnX} style={{
                flex: 1, minWidth: 130, padding: '11px 0', background: '#000', border: '1px solid #333',
                borderRadius: 10, color: C.text, fontWeight: 'bold', fontSize: 14, cursor: 'pointer',
                display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 8,
              }}>
                <svg width="15" height="15" viewBox="0 0 24 24" fill="currentColor"><path d="M18.244 2.25h3.308l-7.227 8.26 8.502 11.24H16.17l-4.714-6.231-5.401 6.231H2.746l7.73-8.835L1.254 2.25H8.08l4.264 5.633 5.9-5.633zm-1.161 17.52h1.833L7.084 4.126H5.117z"/></svg>
                Share on X
              </button>
              <a href={`/node/${nodeCode}`} style={{
                flex: 1, minWidth: 130, padding: '11px 0', background: 'transparent', border: `1px solid ${C.green}`,
                borderRadius: 10, color: C.green, fontWeight: 'bold', fontSize: 14, textDecoration: 'none',
                display: 'flex', alignItems: 'center', justifyContent: 'center',
              }}>Full profile →</a>
            </div>
          </>
        )}
      </div>
    </>
  )
}

function Stat({ label, value, color }: { label: string; value: string; color: string }) {
  return (
    <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', textAlign: 'center', minWidth: 0 }}>
      <div style={{ fontSize: 15, fontWeight: 'bold', color, lineHeight: 1.2, minHeight: 38, display: 'flex', alignItems: 'center' }}>{value}</div>
      <div style={{ fontSize: 10, color: C.muted, marginTop: 4, textTransform: 'uppercase', letterSpacing: 0.8 }}>{label}</div>
    </div>
  )
}

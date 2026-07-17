'use client'

// app/setup/[nodeId]/page.tsx
//
// What a node owner sees after scanning the QR code on their device
// (or visiting network.turbousd.com/setup/A3F2 directly).
//
// First-time section: display name, bio, wallet address, Twitter @,
// country + optional city. These fields enable reward payouts.
//
// Preferences section: alarm, °C/°F, date/time format.
// Verification section: X post link (for verified badge).

import React, { useEffect, useRef, useState } from 'react'
import { supabase, callFunction, FUNCTIONS_BASE_URL } from '@/lib/supabase'
import { InfoModal } from '@/components/NodeBadges'
import TickerBoard from './TickerBoard'

// ── Brand tokens (mirrors treasury.turbousd.com dark theme) ──────────────────
const C = {
  green:    '#43e397',
  greenDim: '#2db876',
  onGreen:  '#000000',
  bg:       '#000000',
  card:     '#0c0c0c',
  border:   '#1c1c1c',
  text:     '#e8e8e8',
  muted:    '#6e7280',
  yellow:   '#ffcf72',
  red:      '#ff6b6b',
  blue:     '#5b8dee',
  surface:  '#141414',
}

// ── Types ─────────────────────────────────────────────────────────────────────
interface NodeConfig {
  node_code:              string
  display_name:           string | null
  bio:                    string | null
  wallet_address:         string | null
  twitter_handle:         string | null
  country:                string | null
  city:                   string | null
  is_verified:            boolean
  is_genesis:             boolean
  temp_unit:              'C' | 'F'
  date_format:            'DD/MM' | 'MM/DD'
  time_format:            '24H' | 'AMPM'
  alarm_hour:             number
  alarm_minute:           number
  alarm_enabled:          boolean
  alarm_volume:           number  // 1–5, default 2
  screen_brightness:      number   // 1–5, default 5 (full)
  screen_always_on:       boolean  // default true
  screen_timeout_mins:    number   // 1 | 5 | 10 | 30, default 10
  screen_carousel:        boolean  // default false
  screen_carousel_secs:   number   // seconds per screen, default 10
  // NFT Gallery (optional, requires DB migration)
  nft_wallet_address?:    string | null
  nft_grid_size?:         1 | 4 | 9
  nft_carousel_enabled?:  boolean
  nft_show_data?:         boolean
  ticker_cols?:           1 | 2
  nft_coll_order?:        string | null
  nft_coll_hidden?:       string | null
  nft_collections?:       { slug: string; name: string; floor: number; btc?: number }[] | null
  screen_hidden?:         string | null
  nft_slideshow_secs?:    number
  // Screen order (optional, requires DB migration)
  // Comma-separated ScreenId integers, e.g. "0,1,2,3,4,5,6". Position 0 is always Home.
  screen_order?:          string | null
  // Manual NFT pinlist (optional, requires DB migration)
  // Comma-separated "chain:contract:tokenId" items, max 20. Takes priority over nft_wallet_address on device.
  nft_pinlist?:           string | null
  // Ticker market-cap alerts ("pool:g|l:usd" CSV, requires DB migration).
  ticker_alerts?:         string | null
  // NFT collection floor alerts ("slug:g|l:value" CSV, requires DB migration).
  nft_alerts?:            string | null
  // Ticker Stats screen selection: which DEX pool the screen shows stats for.
  // Defaults to ₸USD. Chosen here or via the device's footer picker.
  ticker_stats_pool?:     string | null
  ticker_stats_chain?:    string | null
  ticker_stats_symbol?:   string | null
  // Home (first screen) background image URL (1:1 recommended). Empty = black.
  home_bg_url?:           string | null
  // Reported by the device on each heartbeat (the ESP32 image OTA compares against).
  firmware_version?:      string | null
}

interface NodeStats {
  uptime_pct:    number
  total_uptime_seconds: number | null
  uptime_seconds: number | null
  blocks_won:    number
  windows_online: number
  total_tusd_earned: number
}

// Verification steps — SAME text the device shows in its "Verified badge" popup.
const VERIFY_STEPS =
  'To get verified:\n' +
  '1. Post a video on X showing this node running, tagging @turbousd\n' +
  '2. Write your node name on paper, show it matches your screen\n' +
  '3. Include the wallet holding your ₸USD\n' +
  '4. We manually review and whitelist your node'

function fmtUptime(secs: number): string {
  if (secs <= 0) return '—'
  if (secs < 60)    return `${secs}s`
  if (secs < 3600)  return `${Math.floor(secs / 60)}m`
  if (secs < 86400) return `${Math.floor(secs / 3600)}h ${Math.floor((secs % 3600) / 60)}m`
  return `${Math.floor(secs / 86400)}d ${Math.floor((secs % 86400) / 3600)}h`
}

// (Country list removed: location is IP-derived + anonymized, never user-set.)

// ── ENS resolution ────────────────────────────────────────────────────────────
// Lets the NFT wallet field accept "tonysoprano.eth" and swap in the 0x
// address automatically. Uses the public ensideas resolver (CORS-enabled);
// returns null when the name doesn't resolve.
const EVM_RE = /^0x[0-9a-fA-F]{40}$/
async function resolveEns(name: string): Promise<string | null> {
  try {
    const res = await fetch(`https://api.ensideas.com/ens/resolve/${encodeURIComponent(name.trim().toLowerCase())}`)
    if (!res.ok) return null
    const j = await res.json() as { address?: string | null }
    return j.address && EVM_RE.test(j.address) ? j.address : null
  } catch { return null }
}

// ── Page ─────────────────────────────────────────────────────────────────────
export default function NodeSetupPage({ params }: { params: { nodeId: string } }) {
  const nodeCode = params.nodeId.toUpperCase()
  const [node,        setNode]        = useState<NodeConfig | null>(null)
  const [stats,       setStats]       = useState<NodeStats | null>(null)
  const [loading,     setLoading]     = useState(true)
  const [accessDenied, setAccessDenied] = useState(false)
  // Per-device owner secret from the QR on the device's Settings popup
  // (…/setup/CODE?t=TOKEN). Read once on mount; kept for save calls.
  const setupTokenRef = useRef<string>('')
  const [saving,      setSaving]      = useState(false)
  const [saveMsg,     setSaveMsg]     = useState<{ text: string; ok: boolean } | null>(null)

  const [tweetUrl,    setTweetUrl]    = useState('')
  const [verifyBusy,  setVerifyBusy]  = useState(false)
  const [verifyMsg,   setVerifyMsg]   = useState<{ text: string; ok: boolean } | null>(null)

  // NFT Gallery: tab selector + manual pinlist
  const [ensMsg,   setEnsMsg]   = useState<string | null>(null)
  const [pinItems, setPinItems] = useState<PinItem[]>([])
  const pinlistInitRef = useRef(false)

  useEffect(() => {
    // Remember this node code so the network page can show "My Node →" instead of "Setup →"
    localStorage.setItem('turbousd_node_code', nodeCode)

    // Owner-gated load: the config comes from the get-node-setup Edge
    // Function, which requires the per-device setup token (?t=… from the QR
    // in the device's Settings). This also stops "column … does not exist"
    // schema drift from masquerading as "no node found", the function
    // selects * server-side.
    setupTokenRef.current = new URLSearchParams(window.location.search).get('t') ?? ''
    callFunction<{ node: NodeConfig }>('get-node-setup', {
      node_code:   nodeCode,
      setup_token: setupTokenRef.current,
    })
      .then(({ node }) => setNode(node))
      .catch((err: Error) => {
        if (err.message === 'invalid_token') setAccessDenied(true)
      })
      .finally(() => setLoading(false))
    // Fetch stats from the directory view (includes uptime_pct)
    supabase
      .from('public_node_directory')
      .select('uptime_pct, total_uptime_seconds, uptime_seconds, blocks_won, windows_online, total_tusd_earned')
      .eq('node_code', nodeCode)
      .maybeSingle()
      .then(({ data }) => {
        if (data) setStats(data as NodeStats)
      })
  }, [nodeCode])

  // Parse nft_pinlist from DB into PinItem[], once, when node first loads
  useEffect(() => {
    if (!node || pinlistInitRef.current) return
    pinlistInitRef.current = true
    if (!node.nft_pinlist) return
    const items: PinItem[] = node.nft_pinlist.split(',').flatMap(raw => {
      const firstColon  = raw.indexOf(':')
      const secondColon = raw.indexOf(':', firstColon + 1)
      if (firstColon < 1 || secondColon < 1) return []
      // Ordinals may carry a 4th field: a user-picked background colour
      // ("ord:<inscription>:0:#f68b1f") — the on-chain PNGs are transparent
      // and no keyless indexer still serves the Background trait, so the
      // owner chooses the colour here and the device composites onto it.
      let tail = raw.slice(secondColon + 1)
      let bg: string | undefined
      const thirdColon = tail.indexOf(':')
      if (thirdColon > 0) {
        const maybe = tail.slice(thirdColon + 1)
        if (/^#[0-9a-f]{6}$/i.test(maybe)) bg = maybe.toLowerCase()
        tail = tail.slice(0, thirdColon)
      }
      return [{
        chain:    raw.slice(0, firstColon),
        contract: raw.slice(firstColon + 1, secondColon),
        tokenId:  tail,
        bg,
        // name / image_url not stored in DB, shown as plain IDs until user re-adds via UI
      }]
    })
    if (items.length > 0) {
      // Ordinals metadata is derivable locally (name + ordinals.com artwork);
      // EVM picks get their name/thumbnail re-resolved from OpenSea below.
      const withMeta = items.map(i => i.chain === 'ord' ? {
        ...i,
        name:            'Ordinal',
        image_url:       `https://ordinals.com/content/${i.contract}`,
        collection_name: 'Ordinals',
      } : i)
      setPinItems(withMeta)

      // Ordinals: upgrade to the real name + BTC floor in the background
      const ordIds = withMeta.filter(i => i.chain === 'ord').map(i => i.contract)
      if (ordIds.length > 0) {
        callFunction<{ results: { id: string; name?: string | null; collection?: string | null; floor_btc?: number | null }[] }>('resolve-ordinal', { ids: ordIds })
          .then(res => {
            if (!res?.results) return
            setPinItems(prev => prev.map(pI => {
              if (pI.chain !== 'ord') return pI
              const r = res.results.find(x => x.id === pI.contract)
              if (!r) return pI
              return {
                ...pI,
                name: r.name || 'Ordinal',
                // "NodeMonkes · 0.039 ₿" (real collection name when resolved)
                collection_name: [r.collection || 'Ordinals',
                                  r.floor_btc ? `${r.floor_btc.toFixed(3)} ₿` : null]
                                 .filter(Boolean).join(' · '),
              }
            }))
          })
          .catch(() => {})
      }

      const evmIds = withMeta.filter(i => i.chain !== 'ord').map(i => `${i.chain}:${i.contract}:${i.tokenId}`)
      if (evmIds.length > 0) {
        callFunction<{ results: { name?: string; image_url?: string; collection_name?: string; floor_price_eth?: number; error?: string }[] }>('resolve-nft', { items: evmIds })
          .then(res => {
            if (!res?.results) return
            setPinItems(prev => prev.map(pI => {
              if (pI.chain === 'ord') return pI
              const idx = evmIds.indexOf(`${pI.chain}:${pI.contract}:${pI.tokenId}`)
              const r = idx >= 0 ? res.results[idx] : null
              return r && !r.error
                ? { ...pI, name: r.name, image_url: r.image_url, collection_name: r.collection_name, floor_price_eth: r.floor_price_eth }
                : pI
            }))
          })
          .catch(() => { /* thumbnails stay as plain IDs, non-fatal */ })
      }
    }
  }, [node])

  async function handleSave(e: React.FormEvent) {
    e.preventDefault()
    if (!node) return
    setSaving(true)
    setSaveMsg(null)
    try {
      // If the NFT wallet is still an unresolved ENS name (e.g. the user hit
      // Save without leaving the field), resolve it now, the backend only
      // accepts 0x addresses.
      if (node.nft_wallet_address && /\.eth$/i.test(node.nft_wallet_address.trim())) {
        const addr = await resolveEns(node.nft_wallet_address)
        if (!addr) throw new Error(`Could not resolve ${node.nft_wallet_address}`)
        node.nft_wallet_address = addr
        setNode({ ...node })
      }
      await callFunction('update-node-config', {
        node_code:             nodeCode,
        setup_token:           setupTokenRef.current,
        display_name:          node.display_name,
        bio:                   node.bio,
        wallet_address:        node.wallet_address,
        twitter_handle:        node.twitter_handle,
        // country/city are intentionally NOT sent: location is IP-derived and
        // anonymized server-side, never user-editable.
        temp_unit:             node.temp_unit,
        date_format:           node.date_format,
        time_format:           node.time_format,
        alarm_hour:            node.alarm_hour,
        alarm_minute:          node.alarm_minute,
        alarm_enabled:         node.alarm_enabled,
        alarm_volume:          node.alarm_volume ?? 2,
        screen_brightness:     node.screen_brightness ?? 5,
        screen_always_on:      node.screen_always_on  ?? true,
        screen_timeout_mins:   node.screen_timeout_mins ?? 10,
        screen_carousel:       node.screen_carousel ?? false,
        screen_carousel_secs:  node.screen_carousel_secs ?? 10,
        nft_wallet_address:    node.nft_wallet_address,
        nft_grid_size:         node.nft_grid_size,
        nft_carousel_enabled:  node.nft_carousel_enabled,
        nft_show_data:         node.nft_show_data ?? true,
        ticker_cols:           node.ticker_cols ?? 1,
        nft_coll_order:        node.nft_coll_order ?? '',
        nft_coll_hidden:       node.nft_coll_hidden ?? '',
        ticker_alerts:         node.ticker_alerts ?? '',
        nft_alerts:            node.nft_alerts ?? '',
        screen_hidden:         node.screen_hidden ?? '',
        nft_slideshow_secs:    node.nft_slideshow_secs,
        // Serialize pinlist: active if items exist, null to clear (falls back to wallet mode on device)
        nft_pinlist:           pinItems.length > 0
                                 ? pinItems.map(i =>
                                     `${i.chain}:${i.contract}:${i.tokenId}` +
                                     (i.chain === 'ord' && i.bg ? `:${i.bg}` : ''))
                                   .join(',')
                                 : null,
        screen_order:          node.screen_order ?? undefined,
        ticker_stats_pool:     node.ticker_stats_pool   ?? undefined,
        ticker_stats_chain:    node.ticker_stats_chain  ?? undefined,
        ticker_stats_symbol:   node.ticker_stats_symbol ?? undefined,
        home_bg_url:           node.home_bg_url ?? '',
      })
      setSaveMsg({ text: 'Saved. Your device will pick up changes on its next check-in.', ok: true })
    } catch (err) {
      setSaveMsg({ text: err instanceof Error ? err.message : 'Something went wrong.', ok: false })
    } finally {
      setSaving(false)
    }
  }

  async function handleVerifySubmit(e: React.FormEvent) {
    e.preventDefault()
    setVerifyBusy(true)
    setVerifyMsg(null)
    try {
      await callFunction('submit-verification', {
        node_code:  nodeCode,
        tweet_url:  tweetUrl,
        wallet_address: node?.wallet_address ?? '',
      })
      setVerifyMsg({ text: 'Submitted! We review manually, usually within a couple of days.', ok: true })
    } catch (err) {
      setVerifyMsg({ text: err instanceof Error ? err.message : 'Something went wrong.', ok: false })
    } finally {
      setVerifyBusy(false)
    }
  }

  if (loading)      return <Centered>Loading node {nodeCode}…</Centered>
  if (accessDenied) return <AccessDenied nodeCode={nodeCode} />
  if (!node)        return <NotFound nodeCode={nodeCode} />

  return (
    <div style={s.root}>
      <Header nodeCode={node.node_code} isVerified={node.is_verified} isGenesis={node.is_genesis} stats={stats} />

      <div style={s.content}>
        <form onSubmit={handleSave}>

          {/* ── Profile ── */}
          <Section title="Profile" accent={C.green}>
            {!node.is_verified && <VerifyRewardsBanner />}
            <Field label="Node name" hint="Shown on your device and the public network page. Max 24 chars.">
              <input style={s.input} maxLength={24} placeholder="e.g. Satoshi's Garage"
                value={node.display_name ?? ''}
                onChange={e => setNode({ ...node, display_name: e.target.value })}
              />
            </Field>
            <Field label="Bio" hint="A short description shown publicly. Max 160 chars.">
              <textarea style={{ ...s.input, height: 72, resize: 'vertical' }} maxLength={160}
                value={node.bio ?? ''}
                onChange={e => setNode({ ...node, bio: e.target.value })}
              />
            </Field>
            {/* Location is NOT editable: it's derived from the node's IP and
                anonymized to ~300 km before it's ever stored, so there is no
                country/city field to set. The map/cards show that coarse value. */}
            {/* Communities ("projects") the node is part of. Self-saving —
                each add/remove/★ hits its own Edge Function immediately, the
                main Save button is not involved. */}
            <ProjectsEditor nodeCode={nodeCode} />
          </Section>

          {/* ── Rewards ── */}
          <Section title="Rewards & Identity" accent={C.green}>
            <Field
              label="Wallet address"
              hint="EVM address (0x…) on Base network where your ₸USD mining rewards will be sent. Required for payouts."
            >
              <input style={s.input} placeholder="0x…" maxLength={42}
                value={node.wallet_address ?? ''}
                onChange={e => setNode({ ...node, wallet_address: e.target.value })}
              />
            </Field>
            <Field
              label="X / Twitter handle"
              hint="Without the @, used for reward notifications and the verified badge."
            >
              <div style={{ position: 'relative' }}>
                <span style={s.atSign}>@</span>
                <input style={{ ...s.input, paddingLeft: 28 }} placeholder="yourhandle" maxLength={50}
                  value={node.twitter_handle ?? ''}
                  onChange={e => setNode({ ...node, twitter_handle: e.target.value.replace(/^@/, '') })}
                />
              </div>
            </Field>
          </Section>

          {/* ── Alarm ── */}
          <Section title="Alarm" accent={C.yellow}>
            <div style={{ display: 'flex', gap: 12, alignItems: 'center' }}>
              <input type="time" style={{ ...s.input, width: 'auto', flex: 1 }}
                value={`${String(node.alarm_hour).padStart(2, '0')}:${String(node.alarm_minute).padStart(2, '0')}`}
                onChange={e => {
                  const [h, m] = e.target.value.split(':').map(Number)
                  setNode({ ...node, alarm_hour: h, alarm_minute: m })
                }}
              />
              <label style={s.checkboxLabel}>
                <input type="checkbox" checked={node.alarm_enabled}
                  onChange={e => setNode({ ...node, alarm_enabled: e.target.checked })}
                  style={{ accentColor: C.green }}
                />
                Enabled
              </label>
            </div>
            {/* Volume slider */}
            <div style={{ marginTop: 14 }}>
              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 6 }}>
                <span style={{ fontSize: 13, color: C.muted }}>Volume</span>
                <span style={{ fontSize: 13, color: C.text, fontWeight: 600 }}>
                  {['', '🔈 Whisper', '🔉 Soft', '🔉 Medium', '🔊 Loud', '🔊 Max'][(node.alarm_volume ?? 2)]}
                </span>
              </div>
              <input
                type="range" min={1} max={5} step={1}
                value={node.alarm_volume ?? 2}
                onChange={e => setNode({ ...node, alarm_volume: Number(e.target.value) })}
                style={{ width: '100%', accentColor: C.yellow, cursor: 'pointer' }}
              />
              <div style={{ display: 'flex', justifyContent: 'space-between', marginTop: 2 }}>
                {['1', '2', '3', '4', '5'].map(n => (
                  <span key={n} style={{ fontSize: 11, color: C.muted, width: 20, textAlign: 'center' }}>{n}</span>
                ))}
              </div>
            </div>
          </Section>

          {/* ── Display preferences ── */}
          <Section title="Display preferences" accent={C.yellow}>
            {/* Brightness slider */}
            <div style={{ marginBottom: 18 }}>
              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 6 }}>
                <span style={{ fontSize: 13, color: C.muted }}>Brightness</span>
                <span style={{ fontSize: 13, color: C.text, fontWeight: 600 }}>
                  {['', '🌑 Dim', '🌒 Low', '🌓 Medium', '🌔 High', '🌕 Full'][(node.screen_brightness ?? 5)]}
                </span>
              </div>
              <input
                type="range" min={1} max={5} step={1}
                value={node.screen_brightness ?? 5}
                onChange={e => setNode({ ...node, screen_brightness: Number(e.target.value) })}
                style={{ width: '100%', accentColor: C.yellow, cursor: 'pointer' }}
              />
              <div style={{ display: 'flex', justifyContent: 'space-between', marginTop: 2 }}>
                {['1', '2', '3', '4', '5'].map(n => (
                  <span key={n} style={{ fontSize: 11, color: C.muted, width: 20, textAlign: 'center' }}>{n}</span>
                ))}
              </div>
            </div>

            {/* Always-on toggle */}
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', padding: '10px 0', borderTop: `1px solid ${C.border}` }}>
              <span style={{ fontSize: 14, color: C.text }}>Always on</span>
              <label style={{ position: 'relative', display: 'inline-block', width: 44, height: 24, cursor: 'pointer' }}>
                <input
                  type="checkbox"
                  checked={node.screen_always_on ?? true}
                  onChange={e => setNode({ ...node, screen_always_on: e.target.checked })}
                  style={{ opacity: 0, width: 0, height: 0 }}
                />
                <span style={{
                  position: 'absolute', inset: 0, borderRadius: 12,
                  background: (node.screen_always_on ?? true) ? C.green : C.border,
                  transition: 'background 0.2s',
                }}>
                  <span style={{
                    position: 'absolute', top: 3, width: 18, height: 18, borderRadius: '50%', background: '#fff',
                    left: (node.screen_always_on ?? true) ? 23 : 3,
                    transition: 'left 0.2s',
                  }} />
                </span>
              </label>
            </div>

            {/* Turn off after, only visible when always-on is OFF */}
            {!(node.screen_always_on ?? true) && (
              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', padding: '10px 0', borderTop: `1px solid ${C.border}` }}>
                <span style={{ fontSize: 14, color: C.text }}>Turn off after</span>
                <div style={{ display: 'flex', gap: 6 }}>
                  {([1, 5, 10, 30] as const).map(mins => (
                    <button
                      key={mins}
                      type="button"
                      onClick={() => setNode({ ...node, screen_timeout_mins: mins })}
                      style={{
                        padding: '4px 10px', borderRadius: 6, fontSize: 13, cursor: 'pointer', border: 'none',
                        background: (node.screen_timeout_mins ?? 10) === mins ? C.yellow : C.card,
                        color:      (node.screen_timeout_mins ?? 10) === mins ? '#000'    : C.muted,
                        fontWeight: (node.screen_timeout_mins ?? 10) === mins ? 700       : 400,
                      }}
                    >
                      {mins}m
                    </button>
                  ))}
                </div>
              </div>
            )}

            {/* Auto rotate screens (carousel) */}
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', padding: '10px 0', borderTop: `1px solid ${C.border}` }}>
              <span style={{ fontSize: 14, color: C.text }}>Auto rotate screens</span>
              <label style={{ position: 'relative', display: 'inline-block', width: 44, height: 24, cursor: 'pointer' }}>
                <input
                  type="checkbox"
                  checked={node.screen_carousel ?? false}
                  onChange={e => setNode({ ...node, screen_carousel: e.target.checked })}
                  style={{ opacity: 0, width: 0, height: 0 }}
                />
                <span style={{
                  position: 'absolute', inset: 0, borderRadius: 12,
                  background: (node.screen_carousel ?? false) ? C.green : C.border,
                  transition: 'background 0.2s',
                }}>
                  <span style={{
                    position: 'absolute', top: 3, width: 18, height: 18, borderRadius: '50%', background: '#fff',
                    left: (node.screen_carousel ?? false) ? 23 : 3,
                    transition: 'left 0.2s',
                  }} />
                </span>
              </label>
            </div>

            {/* Seconds per screen, only visible when auto-rotate is ON */}
            {(node.screen_carousel ?? false) && (
              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', padding: '10px 0', borderTop: `1px solid ${C.border}` }}>
                <span style={{ fontSize: 14, color: C.text }}>Each screen for</span>
                <div style={{ display: 'flex', gap: 6 }}>
                  {([5, 10, 20, 30, 60] as const).map(secs => (
                    <button
                      key={secs}
                      type="button"
                      onClick={() => setNode({ ...node, screen_carousel_secs: secs })}
                      style={{
                        padding: '4px 10px', borderRadius: 6, fontSize: 13, cursor: 'pointer', border: 'none',
                        background: (node.screen_carousel_secs ?? 10) === secs ? C.yellow : C.card,
                        color:      (node.screen_carousel_secs ?? 10) === secs ? '#000'    : C.muted,
                        fontWeight: (node.screen_carousel_secs ?? 10) === secs ? 700       : 400,
                      }}
                    >
                      {secs}s
                    </button>
                  ))}
                </div>
              </div>
            )}

{/* Temperature (°C/°F) toggle hidden: the base SenseCAP D1 ships without
                an ambient sensor, so this setting currently does nothing. Restore when
                a Grove AHT20 becomes part of the kit.
                        <ToggleRow label="Temperature" value={node.temp_unit}
              options={[['C', '°C'], ['F', '°F']]}
              onChange={v => setNode({ ...node, temp_unit: v as 'C' | 'F' })}
            /> */}
            <ToggleRow label="Date format" value={node.date_format}
              options={[['DD/MM', 'DD/MM'], ['MM/DD', 'MM/DD']]}
              onChange={v => setNode({ ...node, date_format: v as 'DD/MM' | 'MM/DD' })}
            />
            <ToggleRow label="Time format" value={node.time_format}
              options={[['24H', '24h'], ['AMPM', 'AM/PM']]}
              onChange={v => setNode({ ...node, time_format: v as '24H' | 'AMPM' })}
            />

            {/* Home screen background image */}
            <div style={{ paddingTop: 10, borderTop: `1px solid ${C.border}`, marginTop: 6 }}>
              <Field label="Home background image"
                hint="Paste an image URL (a square 1:1 image looks best). The clock, date and alarm get a soft shadow behind them so they stay readable. Leave empty for a plain black background.">
                <div style={{ display: 'flex', gap: 12, alignItems: 'flex-start' }}>
                  <input style={{ ...s.input, flex: 1 }} placeholder="https://…/image.png" maxLength={400}
                    value={node.home_bg_url ?? ''}
                    onChange={e => setNode({ ...node, home_bg_url: e.target.value || null })}
                  />
                  {node.home_bg_url
                    ? <img src={node.home_bg_url} alt="preview"
                        style={{ width: 56, height: 56, borderRadius: 10, objectFit: 'cover', border: `1px solid ${C.border}`, flexShrink: 0 }}
                        onError={e => { (e.target as HTMLImageElement).style.opacity = '0.2' }} />
                    : null}
                </div>
              </Field>
            </div>
          </Section>

          {/* ── Ticker Stats screen ── */}
          <Section title="Ticker Stats Screen" accent={C.yellow}>
            <p style={s.bodyText}>
              The second screen shows detailed stats for a single token. ₸USD is the
              default; pick any other token below and the device shows its price,
              market cap, volume and liquidity (custom tokens like DRB show tailored
              fields). You can also change this from the device itself.
            </p>
            <TickerStatsSelector
              pool={node.ticker_stats_pool     ?? TUSD_STATS.pool}
              chain={node.ticker_stats_chain   ?? TUSD_STATS.chain}
              symbol={node.ticker_stats_symbol ?? TUSD_STATS.symbol}
              onChange={(pool, chain, symbol) => setNode({ ...node, ticker_stats_pool: pool, ticker_stats_chain: chain, ticker_stats_symbol: symbol })}
            />
          </Section>

          {/* ── NFT Gallery ── */}
        <Section title="NFT Gallery" accent={C.blue}>
          <p style={s.bodyText}>
            Your device auto-detects NFTs from a wallet, and you can add hand-picked NFTs
            (or Bitcoin Ordinals) on top. Manual picks are ALWAYS shown in the grid, each
            one takes the cell of the lowest-floor wallet collection.
            Data is fetched from OpenSea and cached 30 minutes on device.
          </p>

          <Field label="NFT wallet address" hint="EVM address (0x…) or ENS name (yourname.eth, resolved automatically). Can differ from your reward wallet. Spam NFTs (floor price = 0) are filtered automatically.">
            <input style={s.input} placeholder="0x… or yourname.eth (defaults to reward wallet if empty)" maxLength={64}
              value={node.nft_wallet_address ?? ''}
              onChange={e => { setEnsMsg(null); setNode({ ...node, nft_wallet_address: e.target.value || null }) }}
              onBlur={async e => {
                const v = e.target.value.trim()
                if (!/\.eth$/i.test(v)) return
                setEnsMsg(`Resolving ${v}…`)
                const addr = await resolveEns(v)
                if (addr) {
                  setNode(n => n ? { ...n, nft_wallet_address: addr } : n)
                  setEnsMsg(`${v} → ${addr.slice(0, 6)}…${addr.slice(-4)}`)
                } else {
                  setEnsMsg(`Could not resolve ${v}, check the name.`)
                }
              }}
            />
            {ensMsg && <p style={{ fontSize: 12, color: ensMsg.startsWith('Could not') ? C.red : C.green, marginTop: 6 }}>{ensMsg}</p>}
          </Field>

          <NftCollectionsBoard
            collections={node.nft_collections ?? null}
            order={node.nft_coll_order ?? ''}
            hidden={node.nft_coll_hidden ?? ''}
            onChange={(ord, hid) => setNode({ ...node, nft_coll_order: ord, nft_coll_hidden: hid })}
            alerts={node.nft_alerts ?? ''}
            onAlertsChange={csv => setNode({ ...node, nft_alerts: csv })}
          />

          {/* Manual picks join the list above: each pick is ALWAYS shown on the
              device, taking the cell of the lowest-floor wallet collection. */}
          <NftPinlistEditor items={pinItems} onChange={setPinItems} />

          {/* Display settings apply to both modes */}
          <div style={{ marginTop: 4 }}>
            <ToggleRow label="Ticker columns" value={String(node.ticker_cols ?? 1)}
              options={[['1', '1 column'], ['2', '2 columns']]}
              onChange={v => setNode({ ...node, ticker_cols: Number(v) as 1 | 2 })}
            />
            <ToggleRow label="Grid size" value={String(node.nft_grid_size ?? 9)}
              options={[['1', '1×1'], ['4', '2×2'], ['9', '3×3']]}
              onChange={v => setNode({ ...node, nft_grid_size: Number(v) as 1 | 4 | 9 })}
            />
            <div style={{ display: 'flex', gap: 16, alignItems: 'center', marginTop: 8, marginBottom: 10, flexWrap: 'wrap' }}>
              <label style={s.checkboxLabel}>
                <input type="checkbox"
                  checked={node.nft_carousel_enabled ?? true}
                  onChange={e => setNode({ ...node, nft_carousel_enabled: e.target.checked })}
                  style={{ accentColor: C.green }}
                />
                Auto-carousel (cycle NFTs per cell)
              </label>
              <label style={s.checkboxLabel}>
                <input type="checkbox"
                  checked={node.nft_show_data ?? true}
                  onChange={e => setNode({ ...node, nft_show_data: e.target.checked })}
                  style={{ accentColor: C.green }}
                />
                Show collection name &amp; floor price
              </label>
            </div>
            <Field label="Slideshow interval (seconds)" hint="How long each NFT is shown before advancing. Set 0 to disable.">
              <input type="number" style={{ ...s.input, width: 100 }} min={0} max={120}
                value={node.nft_slideshow_secs ?? 10}
                onChange={e => setNode({ ...node, nft_slideshow_secs: Number(e.target.value) })}
              />
            </Field>
          </div>
        </Section>

        {/* ── Screen order ── */}
        <Section title="Screen order" accent={C.yellow}>
          <p style={s.bodyText}>
            Drag to reorder the screens on your device. <strong style={{ color: C.text }}>Home</strong> is always first.
          </p>
          <ScreenOrderSection
            value={node.screen_order ?? null}
            hidden={node.screen_hidden ?? ''}
            onChange={order => setNode({ ...node, screen_order: order })}
            onHiddenChange={h => setNode({ ...node, screen_hidden: h })}
          />
        </Section>

          {/* One Save button for EVERYTHING above (profile, alarm, display,
              NFT gallery, screen order). STICKY at the viewport bottom: the
              page grew long enough that editing near the top meant scrolling
              forever to find Save — now it's always in reach. */}
          <div style={{ position: 'sticky', bottom: 0, zIndex: 20, padding: '12px 0',
                        background: 'linear-gradient(to top, #000000 75%, transparent)' }}>
            <button type="submit" disabled={saving} style={s.primaryBtn}>
              {saving ? 'Saving…' : 'Save changes'}
            </button>
            {saveMsg && (
              <p style={{ ...s.msg, color: saveMsg.ok ? C.green : C.red, marginBottom: 0 }}>{saveMsg.text}</p>
            )}
          </div>
        </form>

        {/* ── Token screener, same left-accent treatment as the other sections ── */}
        <div style={{ ...s.section, borderLeftColor: C.green, marginTop: 36 }}>
          <TickerBoard
            nodeCode={nodeCode}
            isOwner={true}
            alerts={node.ticker_alerts ?? ''}
            onAlertsChange={csv => setNode({ ...node, ticker_alerts: csv })}
          />
        </div>

        {/* ── Verification ── */}
        <Section title="Verified badge" accent={C.green}>
          {node.is_verified ? (
            <p style={{ color: C.green, fontSize: 14 }}>✓ This node is verified. Your ₸USD rewards will be paid to the configured wallet.</p>
          ) : (
            <>
              <p style={s.bodyText}>
                To get the <strong style={{ color: C.green }}>✓</strong> badge and start receiving rewards:
                post a short video on X tagging <strong style={{ color: C.green }}>@turbousd</strong> showing
                your device screen with your node name, then submit the X link below.
                Make sure your wallet address is saved first.
              </p>
              <form onSubmit={handleVerifySubmit}>
                <Field label="X post URL">
                  <input style={s.input} placeholder="https://x.com/yourhandle/status/…"
                    value={tweetUrl}
                    onChange={e => setTweetUrl(e.target.value)}
                  />
                </Field>
                <button type="submit" disabled={verifyBusy} style={s.outlineBtn}>
                  {verifyBusy ? 'Submitting…' : 'Submit for review'}
                </button>
                {verifyMsg && (
                  <p style={{ ...s.msg, color: verifyMsg.ok ? C.green : C.red }}>{verifyMsg.text}</p>
                )}
              </form>
            </>
          )}
        </Section>

        {/* ── Device buttons (info) ── */}
        <Section title="Device buttons" accent={C.muted}>
          <p style={s.bodyText}>
            Your SenseCAP D1 has two buttons:
          </p>
          <ul style={{ margin: '8px 0 0', paddingLeft: 18, fontSize: 14, color: C.muted, lineHeight: 1.6 }}>
            <li>
              <strong style={{ color: C.text }}>Top button</strong>: tap to turn the screen off/on
              (the node keeps mining in the background), or to silence a ringing alarm.{' '}
              <strong style={{ color: C.text }}>Double-tap</strong> on the NFT gallery to enter fullscreen
              &quot;photo frame&quot; mode (double-tap again to exit). It{' '}
              <strong style={{ color: C.text }}>won&apos;t</strong> erase your firmware, there&apos;s
              no reset shortcut, on purpose.
            </li>
            <li style={{ marginTop: 4 }}>
              <strong style={{ color: C.text }}>Bottom pinhole</strong> (next to USB-C), only used when
              re-flashing the sensor chip; you press it with a paperclip during setup.
            </li>
          </ul>
        </Section>

        {/* ── Firmware & updates (always last) ── */}
        <Section title="Firmware & updates" accent={C.muted}>
          <FirmwareCheck current={node.firmware_version} />
        </Section>
      </div>
    </div>
  )
}

// Firmware version + OTA check. The web can't push OTA to the device (the
// device pulls updates itself), so this reports the current version, checks the
// latest published ESP32 image, and if newer tells the user to install it from
// the device's Settings → Check for updates (which does the WiFi OTA).
function FirmwareCheck({ current }: { current?: string | null }) {
  const [msg, setMsg] = useState<{ text: string; ok: boolean } | null>(null)
  const [notes, setNotes] = useState<string | null>(null)   // changelog for the newer version
  const [busy, setBusy] = useState(false)
  async function check() {
    setBusy(true); setMsg(null); setNotes(null)
    try {
      const anon = process.env.NEXT_PUBLIC_SUPABASE_ANON_KEY ?? ''
      const res = await fetch(`${FUNCTIONS_BASE_URL}/latest-firmware?target=esp32s3`, {
        headers: { Authorization: `Bearer ${anon}`, apikey: anon },
      })
      if (res.status === 404) {
        setMsg({ text: 'No firmware release has been published yet.', ok: true })
        return
      }
      const j = await res.json()
      const latest = j?.version as string | undefined
      if (!res.ok || !latest) throw new Error(j?.error ?? 'Could not fetch the latest firmware info.')
      const cur = current && current !== 'unknown' ? current : null
      if (cur && cur === latest) {
        setMsg({ text: `You're on the latest firmware (v${latest}).`, ok: true })
      } else {
        setMsg({
          text: `Update available: v${latest}${cur ? ` (your node reports v${cur})` : ''}. On the device, open Settings (gear icon) and tap "Check for updates" to install it over WiFi.`,
          ok: false,
        })
        // Show the changelog for the new version when the release carries one.
        if (typeof j?.release_notes === 'string' && j.release_notes.trim()) setNotes(j.release_notes.trim())
      }
    } catch (e) {
      setMsg({ text: e instanceof Error ? e.message : 'Check failed.', ok: false })
    } finally { setBusy(false) }
  }
  return (
    <div style={{ marginTop: 14 }}>
      <p style={{ margin: '0 0 8px', fontSize: 13, color: C.muted }}>
        Firmware: <strong style={{ color: C.text }}>ESP32 v{current || 'unknown'}</strong> &middot; RP2040 v0.1.0.
        The ESP32 image updates over the air.
      </p>
      <p style={{ margin: '0 0 10px', fontSize: 12, color: C.muted, lineHeight: 1.5 }}>
        You can also update straight from the node: on the device open{' '}
        <strong style={{ color: C.text }}>Settings</strong> (the gear icon, at the very bottom) and tap{' '}
        <strong style={{ color: C.text }}>Check for updates</strong> to install the latest over WiFi.
      </p>
      <button onClick={check} disabled={busy} style={{
        padding: '8px 16px', background: '#1b2438', border: '1px solid #4a6aa8', borderRadius: 8,
        color: '#dbe7ff', fontSize: 13, fontWeight: 600, cursor: busy ? 'default' : 'pointer', opacity: busy ? 0.6 : 1,
      }}>{busy ? 'Checking…' : 'Check for updates'}</button>
      {msg && <p style={{ marginTop: 8, fontSize: 13, color: msg.ok ? C.green : '#ffcf72' }}>{msg.text}</p>}
      {notes && (
        <div style={{ marginTop: 10, background: C.surface, border: `1px solid ${C.border}`, borderRadius: 8, padding: '10px 12px' }}>
          <div style={{ fontSize: 12, fontWeight: 700, color: C.text, marginBottom: 6, letterSpacing: 0.5 }}>WHAT&apos;S NEW</div>
          <pre style={{ margin: 0, whiteSpace: 'pre-wrap', fontFamily: 'inherit', fontSize: 12, color: C.muted, lineHeight: 1.6 }}>{notes}</pre>
        </div>
      )}
    </div>
  )
}

// ── Sub-components ────────────────────────────────────────────────────────────

function Header({ nodeCode, isVerified, stats }: {
  nodeCode:   string
  isVerified: boolean
  isGenesis:  boolean
  stats:      NodeStats | null
}) {
  return (
    <>
      <header style={s.header}>
        <a href="/" style={s.back}>← Network</a>
        <span style={s.logo}>
          {nodeCode}
          {isVerified && <span style={s.badge}>✓</span>}
        </span>
        {/* Right zone mirrors the left zone's flex so the title is TRULY centered */}
        <span style={{ flex: '1 0 0', display: 'flex', justifyContent: 'flex-end' }}>
          <a href="/setup" style={s.setupLink}>Flash new device</a>
        </span>
      </header>
      {stats && (
        <div style={s.statsBar}>
          <StatChip label="Uptime"   value={fmtUptime(stats.total_uptime_seconds ?? stats.uptime_seconds ?? 0)} color="#d2d2d8" />
          <StatChip label="Earned"   value={`₸${stats.total_tusd_earned.toFixed(2)}`} color="#43e397" />
          <StatChip label="Blocks"   value={String(stats.blocks_won)}   color="#5b8dee" />
        </div>
      )}
    </>
  )
}

// Shown until the node is verified: prompts the owner to verify to receive
// rewards. "verified" is underlined and opens the same steps the device shows.
function VerifyRewardsBanner() {
  const [open, setOpen] = useState(false)
  return (
    <Banner color={C.yellow}>
      Your node must be{' '}
      <span
        onClick={() => setOpen(true)}
        style={{ textDecoration: 'underline', cursor: 'pointer', fontWeight: 700 }}
      >verified</span>
      {' '}to receive ₸USD rewards.
      {open && <InfoModal title="Verify your node" body={VERIFY_STEPS} onClose={() => setOpen(false)} />}
    </Banner>
  )
}

function StatChip({ label, value, color }: { label: string; value: string; color: string }) {
  return (
    <div style={{ textAlign: 'center' }}>
      <div style={{ fontSize: 15, fontWeight: 'bold', color }}>{value}</div>
      <div style={{ fontSize: 10, color: '#6e7280', marginTop: 3, textTransform: 'uppercase', letterSpacing: 0.8 }}>{label}</div>
    </div>
  )
}

function NotFound({ nodeCode }: { nodeCode: string }) {
  return (
    <div style={{ minHeight: '100vh', background: C.bg, color: C.text, fontFamily: 'system-ui, -apple-system, sans-serif' }}>
      <header style={s.header}>
        <a href="/" style={s.back}>← Network</a>
        <span style={s.logo}>TurboUSD Node {nodeCode}</span>
        <div style={{ width: 120 }} />
      </header>
      <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', minHeight: 'calc(100vh - 56px)', padding: 24 }}>
        <p style={{ color: C.muted, fontSize: 15, textAlign: 'center', marginBottom: 24, lineHeight: 1.6 }}>
          No node found with code <strong style={{ color: C.text }}>{nodeCode}</strong>.<br />
          Open <strong style={{ color: C.text }}>Settings</strong> on your device (tap the QR icon in the footer) and
          scan the QR code, or type the exact URL shown right below it.
        </p>
        <a href="/setup" style={{
          padding: '11px 24px', background: 'transparent', color: C.text,
          border: '1px solid #3a3a3a', borderRadius: 8, fontWeight: 'bold',
          fontSize: 14, textDecoration: 'none', cursor: 'pointer',
        }}>
          ← Back to setup
        </a>
      </div>
    </div>
  )
}

function AccessDenied({ nodeCode }: { nodeCode: string }) {
  return (
    <div style={{ minHeight: '100vh', background: C.bg, color: C.text, fontFamily: 'system-ui, -apple-system, sans-serif' }}>
      <header style={s.header}>
        <a href="/" style={s.back}>← Network</a>
        <span style={s.logo}>TurboUSD Node {nodeCode}</span>
        <div style={{ width: 120 }} />
      </header>
      <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', minHeight: 'calc(100vh - 56px)', padding: 24 }}>
        <p style={{ color: C.muted, fontSize: 15, textAlign: 'center', marginBottom: 24, lineHeight: 1.6, maxWidth: 420 }}>
          This setup link is missing its <strong style={{ color: C.text }}>owner access token</strong>, so it can't be opened.<br /><br />
          Only the person holding the device can edit it: open <strong style={{ color: C.text }}>Settings</strong> on
          your device (tap the QR icon in the footer) and <strong style={{ color: C.text }}>scan the QR code</strong>,
          it opens this page with the correct token. You can also type the exact URL shown right below the QR.
        </p>
        <div style={{ display: 'flex', gap: 10, flexWrap: 'wrap', justifyContent: 'center' }}>
          <a href={`/node/${nodeCode}`} style={{
            padding: '11px 24px', background: 'transparent', color: C.text,
            border: '1px solid #3a3a3a', borderRadius: 8, fontWeight: 'bold',
            fontSize: 14, textDecoration: 'none', cursor: 'pointer',
          }}>
            View public profile →
          </a>
          <a href="/my-node" style={{
            padding: '11px 24px', background: 'transparent', color: C.text,
            border: '1px solid #3a3a3a', borderRadius: 8, fontWeight: 'bold',
            fontSize: 14, textDecoration: 'none', cursor: 'pointer',
          }}>
            Enter a node code →
          </a>
        </div>
      </div>
    </div>
  )
}

// ₸USD is the Ticker Stats default (its own Base pool).
const TUSD_STATS = { pool: '0xd013725b904e76394A3aB0334Da306C505D778F8', chain: 'base', symbol: 'TUSD' }

// Single-select token search for the Ticker Stats screen. Same DexScreener
// source + liquidity filter as the device's on-screen search; picking a token
// stores its pool/chain/symbol (saved with the main Save button, then synced to
// the device on its next heartbeat).
function TickerStatsSelector({ pool, chain, symbol, onChange }: {
  pool: string; chain: string; symbol: string
  onChange: (pool: string, chain: string, symbol: string) => void
}) {
  const [query, setQuery]     = useState('')
  const [results, setResults] = useState<{ pool: string; chain: string; symbol: string; name: string; liq: number }[]>([])
  const [loading, setLoading] = useState(false)
  const isTusd = pool.toLowerCase() === TUSD_STATS.pool.toLowerCase()

  async function search() {
    if (query.trim().length < 2) return
    setLoading(true)
    try {
      const res = await fetch(`https://api.dexscreener.com/latest/dex/search?q=${encodeURIComponent(query.trim())}`)
      if (!res.ok) throw new Error(`DexScreener ${res.status}`)
      const data = await res.json() as { pairs?: Array<{
        pairAddress: string; chainId: string; liquidity?: { usd?: number }
        baseToken?: { symbol?: string; name?: string }
      }> }
      setResults((data.pairs ?? [])
        .filter(pr => (pr.liquidity?.usd ?? 0) >= 1000)
        .slice(0, 10)
        .map(pr => ({
          pool:   pr.pairAddress,
          chain:  pr.chainId,
          symbol: pr.baseToken?.symbol ?? '?',
          name:   pr.baseToken?.name ?? '',
          liq:    pr.liquidity?.usd ?? 0,
        })))
    } catch { setResults([]) }
    setLoading(false)
  }

  const btn: React.CSSProperties = {
    padding: '8px 14px', background: 'transparent', color: C.green,
    border: `1px solid ${C.green}`, borderRadius: 8, fontWeight: 'bold',
    fontSize: 13, cursor: 'pointer', whiteSpace: 'nowrap',
  }

  return (
    <div>
      {/* Current selection */}
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 12,
                    background: C.surface, border: `1px solid ${C.border}`, borderRadius: 8, padding: '10px 12px' }}>
        <div style={{ minWidth: 0 }}>
          <div style={{ fontSize: 14, color: C.text, fontWeight: 700 }}>
            {symbol || 'TUSD'}{isTusd ? '  (₸USD · default)' : ''}
          </div>
          <div style={{ fontSize: 11, color: C.muted, marginTop: 2 }}>
            {chain} · {pool.slice(0, 6)}…{pool.slice(-4)}
          </div>
        </div>
        {!isTusd && (
          <button type="button" style={{ ...btn, color: C.muted, borderColor: C.border }}
            onClick={() => onChange(TUSD_STATS.pool, TUSD_STATS.chain, TUSD_STATS.symbol)}>
            Reset to ₸USD
          </button>
        )}
      </div>

      {/* Search */}
      <div style={{ display: 'flex', gap: 8 }}>
        <input style={s.input} value={query} onChange={e => setQuery(e.target.value)}
          onKeyDown={e => e.key === 'Enter' && search()}
          placeholder="Search a token to show its stats…" />
        <button type="button" style={btn} onClick={search} disabled={loading}>
          {loading ? '…' : 'Search'}
        </button>
      </div>

      {results.length > 0 && (
        <div style={{ marginTop: 8, border: `1px solid ${C.border}`, borderRadius: 8, overflow: 'hidden' }}>
          {results.map(r => (
            <div key={r.pool + r.chain} style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center',
                                                  padding: '8px 12px', borderTop: `1px solid ${C.border}` }}>
              <div style={{ minWidth: 0 }}>
                <span style={{ fontWeight: 700, fontSize: 13, color: C.text }}>{r.name || r.symbol}</span>
                <span style={{ fontSize: 11, color: C.muted, marginLeft: 6 }}>
                  {r.symbol} · {r.chain} · Liq ${Math.round(r.liq).toLocaleString()}
                </span>
              </div>
              <button type="button" style={{ ...btn, padding: '6px 12px' }}
                onClick={() => { onChange(r.pool, r.chain, r.symbol); setResults([]); setQuery('') }}>
                Select
              </button>
            </div>
          ))}
        </div>
      )}
    </div>
  )
}

function Section({ title, accent, children }: { title: string; accent: string; children: React.ReactNode }) {
  return (
    <div style={{ ...s.section, borderLeftColor: accent }}>
      <h2 style={s.sectionTitle}>{title}</h2>
      {children}
    </div>
  )
}

function Field({ label, hint, children }: { label: string; hint?: string; children: React.ReactNode }) {
  return (
    <div style={{ marginBottom: 16, flex: 1 }}>
      <label style={s.label}>{label}</label>
      {children}
      {hint && <p style={s.hint}>{hint}</p>}
    </div>
  )
}

function Row({ children }: { children: React.ReactNode }) {
  return <div style={{ display: 'flex', gap: 12 }}>{children}</div>
}

function Banner({ color, children }: { color: string; children: React.ReactNode }) {
  return (
    <div style={{ background: `${color}12`, border: `1px solid ${color}40`, borderRadius: 8, padding: '10px 14px', fontSize: 13, color, marginBottom: 16, lineHeight: 1.6 }}>
      {children}
    </div>
  )
}

function ToggleRow({ label, value, options, onChange }: {
  label: string; value: string; options: [string, string][]; onChange: (v: string) => void
}) {
  return (
    <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginBottom: 10 }}>
      <span style={{ color: C.muted, fontSize: 13 }}>{label}</span>
      <div style={{ display: 'flex', border: `1px solid ${C.green}`, borderRadius: 6, overflow: 'hidden' }}>
        {options.map(([val, lbl]) => (
          <button key={val} type="button" onClick={() => onChange(val)} style={{
            padding: '6px 14px', fontSize: 12, fontWeight: 'bold',
            background: value === val ? C.green : 'transparent',
            color: value === val ? C.onGreen : C.muted,
            border: 'none', cursor: 'pointer',
          }}>{lbl}</button>
        ))}
      </div>
    </div>
  )
}

// ── Screen label map ─────────────────────────────────────────────────────────
// Keys are ScreenId enum values from firmware ui_manager.h.
// Default swipe order: Home → TurboStats → Tickers → Debt → Inflation → NFT → My Node
const SCREEN_LABELS: Record<number, string> = {
  0: 'Home',        // CLOCK, always fixed first
  1: 'Ticker Stats',  // TURBO_STATS
  2: 'US Debt',     // DEBT
  3: 'Inflation',   // INFLATION_GAME
  4: 'Mining',      // NODE_NETWORK (live mining view)
  5: 'NFTs',        // NFT
  6: 'Tickers',     // TICKERS
}

// Matches the firmware's default _swipeOrder = {0,1,6,5,2,3,4}:
// Home → TurboStats → Tickers → NFTs → US Debt → Inflation → Mining.
const DEFAULT_ORDER = [0, 1, 6, 5, 2, 3, 4]

function parseOrder(raw: string | null): number[] {
  if (!raw) return DEFAULT_ORDER
  const parts = raw.split(',').map(s => parseInt(s.trim(), 10))
  if (parts.length === 7 && parts.every(n => n >= 0 && n <= 6) && new Set(parts).size === 7 && parts[0] === 0)
    return parts
  return DEFAULT_ORDER
}

// ── NFT collections board, one row per collection the DEVICE detected in
// the wallet (reported on each heartbeat). Checkbox = shown on the device;
// arrows reorder; the first 9 checked rows fill the 3×3 grid (4 for 2×2,
// 1 for 1×1). Order/hidden are stored as comma-joined slug lists.
const collBtnStyle: React.CSSProperties = {
  background: 'none', border: `1px solid ${C.border}`, borderRadius: 4,
  color: C.muted, cursor: 'pointer', width: 26, height: 26, fontSize: 12,
  lineHeight: 1, flexShrink: 0,
}

function NftCollectionsBoard({ collections, order, hidden, onChange, alerts = '', onAlertsChange }: {
  collections: { slug: string; name: string; floor: number; btc?: number }[] | null
  order: string
  hidden: string
  onChange: (order: string, hidden: string) => void
  alerts?: string                          // "slug:g|l:value" CSV (node.nft_alerts)
  onAlertsChange?: (csv: string) => void   // lifts edits into the page's node state
}) {
  // Floor-alert editor state (must be declared before any early return).
  const [editingAlert, setEditingAlert] = React.useState<string | null>(null)
  const [aDir, setADir] = React.useState<'g' | 'l'>('g')
  const [aVal, setAVal] = React.useState('')
  if (!collections || collections.length === 0) {
    return (
      <p style={{ fontSize: 12, color: C.muted, margin: '10px 0' }}>
        No collections reported yet, the device sends the detected list a few
        minutes after it loads the gallery for the first time.
      </p>
    )
  }
  const hiddenSet = new Set(hidden.split(',').map(x => x.trim()).filter(Boolean))
  // Effective order: listed slugs first (in saved order), then the rest by reported (floor) order
  const bySlug = new Map(collections.map(c => [c.slug, c]))
  const ordered: { slug: string; name: string; floor: number; btc?: number }[] = []
  for (const slug of order.split(',').map(x => x.trim()).filter(Boolean)) {
    const c = bySlug.get(slug)
    if (c) { ordered.push(c); bySlug.delete(slug) }
  }
  for (const c of collections) if (bySlug.has(c.slug)) { ordered.push(c); bySlug.delete(c.slug) }

  // Pure floor order (most valuable first) = the baseline the device sorts by.
  const floorSlugs = [...collections].sort((a, b) => b.floor - a.floor).map(c => c.slug)

  // Store a SPARSE overlay: only the leading run that diverges from floor order.
  // Writing the whole list (what a checkbox toggle or a single move used to do)
  // froze a stale ranking that then overrode the device's USD floor sort.
  const sparse = (list: typeof ordered) => {
    const slugs = list.map(c => c.slug)
    let keep = 0
    for (let k = 0; k < slugs.length; k++)
      if (k >= floorSlugs.length || slugs[k] !== floorSlugs[k]) keep = k + 1
    return slugs.slice(0, keep).join(',')
  }

  // Hiding/showing must NOT rewrite the order (that was the main accidental
  // freeze): keep the existing order string, only update the hidden set.
  const commitHidden = (hs: Set<string>) => onChange(order, [...hs].join(','))

  // Floor alerts ("slug:g|l:value" CSV, value in the collection's floor
  // currency). Mirrors the ticker alerts; saved with the page's Save button.
  const alertMap: Record<string, { dir: 'g' | 'l'; val: number }> = {}
  for (const e of (alerts || '').split(',')) {
    const p = e.trim().split(':')
    if (p.length === 3 && (p[1] === 'g' || p[1] === 'l')) {
      const v = parseFloat(p[2])
      if (v > 0) alertMap[p[0]] = { dir: p[1], val: v }
    }
  }
  const serializeNftAlerts = (m: typeof alertMap) =>
    Object.entries(m).map(([slug, a]) => `${slug}:${a.dir}:${a.val.toFixed(2)}`).join(',')
  const openAlertEditor = (slug: string) => {
    const cur = alertMap[slug]
    setADir(cur?.dir ?? 'g')
    setAVal(cur ? cur.val.toFixed(2).replace(/\.?0+$/, '') : '')
    setEditingAlert(slug)
  }
  const saveAlert = (slug: string) => {
    const num = parseFloat(aVal)
    if (!(num > 0)) return
    onAlertsChange?.(serializeNftAlerts({ ...alertMap, [slug]: { dir: aDir, val: Math.round(num * 100) / 100 } }))
    setEditingAlert(null)
  }
  const clearAlert = (slug: string) => {
    const m = { ...alertMap }
    delete m[slug]
    onAlertsChange?.(serializeNftAlerts(m))
    setEditingAlert(null)
  }

  const move = (idx: number, dir: number) => {
    const to = idx + dir
    if (to < 0 || to >= ordered.length) return
    const next = [...ordered]
    ;[next[idx], next[to]] = [next[to], next[idx]]
    onChange(sparse(next), [...hiddenSet].join(','))
  }

  let visibleRank = 0
  return (
    <div style={{ marginTop: 6, marginBottom: 12 }}>
      <p style={{ fontSize: 12, color: C.muted, marginBottom: 8 }}>
        Collections detected in the wallet, check to show on the device, reorder
        with the arrows. The first <strong style={{ color: C.text }}>9 checked</strong> fill
        the grid (top-left first).
      </p>
      {ordered.map((c, idx) => {
        const isHidden = hiddenSet.has(c.slug)
        const rank = !isHidden ? ++visibleRank : 0
        const inGrid = rank > 0 && rank <= 9
        return (
          <div key={c.slug} style={{
            display: 'flex', alignItems: 'center', gap: 10,
            padding: '8px 12px', marginBottom: 5, borderRadius: 8,
            background: C.card, border: `1px solid ${inGrid ? C.blue : C.border}`,
            opacity: isHidden ? 0.5 : 1,
          }}>
            <input type="checkbox" checked={!isHidden} style={{ accentColor: C.blue, flexShrink: 0 }}
              onChange={e => {
                const hs = new Set(hiddenSet)
                if (e.target.checked) hs.delete(c.slug); else hs.add(c.slug)
                commitHidden(hs)
              }}
            />
            <span style={{ fontSize: 11, color: inGrid ? C.blue : C.muted, width: 22, textAlign: 'center', flexShrink: 0 }}>
              {rank > 0 ? `#${rank}` : '—'}
            </span>
            <span style={{ flex: 1, fontSize: 13, fontWeight: 600, color: C.text, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
              {c.name || c.slug}
            </span>
            <span style={{ fontSize: 12, color: C.muted, flexShrink: 0 }}>
              {c.floor >= 0.01 ? c.floor.toFixed(2) : c.floor.toFixed(3)} {c.btc ? '₿' : 'Ξ'}
            </span>
            {onAlertsChange && (
              <button type="button"
                style={{ ...collBtnStyle, color: alertMap[c.slug] ? '#ffcf72' : C.muted }}
                title={alertMap[c.slug]
                  ? `Floor alert: ${alertMap[c.slug].dir === 'g' ? '>' : '<'} ${alertMap[c.slug].val}`
                  : 'Set floor alert'}
                onClick={() => (editingAlert === c.slug ? setEditingAlert(null) : openAlertEditor(c.slug))}
              >🔔</button>
            )}
            <button type="button" style={{ ...collBtnStyle, opacity: idx > 0 ? 1 : 0.25 }} disabled={idx === 0}
              onClick={() => move(idx, -1)}>▲</button>
            <button type="button" style={{ ...collBtnStyle, opacity: idx < ordered.length - 1 ? 1 : 0.25 }} disabled={idx === ordered.length - 1}
              onClick={() => move(idx, 1)}>▼</button>
          </div>
        )
      })}
      {editingAlert && (
        <div style={{ display: 'flex', alignItems: 'center', gap: 8, flexWrap: 'wrap',
                      padding: '10px 12px', marginTop: 4, background: '#0a0a0a',
                      border: `1px solid ${C.border}`, borderRadius: 8 }}>
          <span style={{ fontSize: 12, color: C.muted }}>
            {editingAlert}: ring when floor goes
          </span>
          <select value={aDir} onChange={e => setADir(e.target.value as 'g' | 'l')}
            style={{ background: '#141414', color: C.text, border: `1px solid ${C.border}`, borderRadius: 6, padding: '6px 8px', fontSize: 13 }}>
            <option value="g">above &gt;</option>
            <option value="l">below &lt;</option>
          </select>
          <input type="number" min="0" step="0.01" placeholder="0.50" value={aVal}
            onChange={e => setAVal(e.target.value)}
            style={{ background: '#141414', color: C.text, border: `1px solid ${C.border}`, borderRadius: 6, padding: '6px 8px', fontSize: 13, width: 90 }} />
          <span style={{ fontSize: 13, color: C.text, fontWeight: 700 }}>
            {collections.find(c => c.slug === editingAlert)?.btc ? '₿ BTC' : 'Ξ ETH'}
          </span>
          <button type="button" onClick={() => saveAlert(editingAlert)}
            style={{ background: 'transparent', color: '#43e397', border: '1px solid #43e397', borderRadius: 6, padding: '6px 12px', fontSize: 13, cursor: 'pointer' }}>Set</button>
          {alertMap[editingAlert] && (
            <button type="button" onClick={() => clearAlert(editingAlert)}
              style={{ background: 'transparent', color: '#ff6b6b', border: '1px solid #ff6b6b', borderRadius: 6, padding: '6px 12px', fontSize: 13, cursor: 'pointer' }}>Clear</button>
          )}
        </div>
      )}
    </div>
  )
}

function ScreenOrderSection({ value, hidden, onChange, onHiddenChange }: {
  value: string | null
  hidden: string
  onChange: (v: string) => void
  onHiddenChange: (v: string) => void
}) {
  const hiddenSet = new Set(hidden.split(',').map(x => x.trim()).filter(Boolean))
  const toggleHidden = (screenId: number) => {
    const hs = new Set(hiddenSet)
    const key = String(screenId)
    if (hs.has(key)) hs.delete(key); else hs.add(key)
    onHiddenChange([...hs].join(','))
  }
  const [order, setOrder] = React.useState<number[]>(() => parseOrder(value))
  const [dragSrc, setDragSrc] = React.useState<number | null>(null)
  const [dragOver, setDragOver] = React.useState<number | null>(null)

  // Keep local state in sync when parent resets
  React.useEffect(() => { setOrder(parseOrder(value)) }, [value])

  function commit(newOrder: number[]) {
    setOrder(newOrder)
    onChange(newOrder.join(','))
  }

  function handleDragStart(idx: number) { setDragSrc(idx) }
  function handleDragOver(e: React.DragEvent, idx: number) {
    e.preventDefault()
    if (idx !== 0) setDragOver(idx)  // can't drop on Home
  }
  function handleDrop(e: React.DragEvent, targetIdx: number) {
    e.preventDefault()
    if (dragSrc === null || dragSrc === 0 || targetIdx === 0) { setDragSrc(null); setDragOver(null); return }
    const next = [...order]
    const [moved] = next.splice(dragSrc, 1)
    next.splice(targetIdx, 0, moved)
    next[0] = 0  // ensure Home stays first
    setDragSrc(null); setDragOver(null)
    commit(next)
  }

  function moveUp(idx: number) {
    if (idx <= 1) return  // can't move above Home
    const next = [...order]
    ;[next[idx - 1], next[idx]] = [next[idx], next[idx - 1]]
    commit(next)
  }
  function moveDown(idx: number) {
    if (idx >= order.length - 1) return
    const next = [...order]
    ;[next[idx], next[idx + 1]] = [next[idx + 1], next[idx]]
    commit(next)
  }

  return (
    <div style={{ userSelect: 'none' }}>
      {order.map((screenId, idx) => {
        const isHome = idx === 0
        const isDragging = dragSrc === idx
        const isTarget = dragOver === idx && !isHome
        return (
          <div
            key={screenId}
            draggable={!isHome}
            onDragStart={() => handleDragStart(idx)}
            onDragOver={e => handleDragOver(e, idx)}
            onDrop={e => handleDrop(e, idx)}
            onDragEnd={() => { setDragSrc(null); setDragOver(null) }}
            style={{
              display: 'flex', alignItems: 'center', gap: 10,
              padding: '10px 12px', marginBottom: 6, borderRadius: 8,
              background: isTarget ? `${C.yellow}14` : isHome ? C.surface : C.card,
              border: `1px solid ${isTarget ? C.yellow : isHome ? C.border : isDragging ? C.green : C.border}`,
              opacity: isDragging ? 0.4 : 1,
              cursor: isHome ? 'default' : 'grab',
              transition: 'border-color .15s, background .15s',
            }}
          >
            {/* drag handle or lock icon */}
            <span style={{ fontSize: 14, color: isHome ? C.border : C.muted, flexShrink: 0, lineHeight: 1 }}>
              {isHome ? '🔒' : '⠿'}
            </span>

            {/* position number */}
            <span style={{ fontSize: 11, color: C.muted, width: 18, textAlign: 'center', flexShrink: 0 }}>
              {idx + 1}
            </span>

            {/* screen name */}
            <span style={{
              flex: 1, fontSize: 14, fontWeight: 600,
              color: isHome ? C.muted : hiddenSet.has(String(screenId)) ? C.muted : C.text,
              textDecoration: hiddenSet.has(String(screenId)) ? 'line-through' : 'none',
              opacity: hiddenSet.has(String(screenId)) ? 0.6 : 1,
            }}>
              {SCREEN_LABELS[screenId] ?? `Screen ${screenId}`}
            </span>

            {/* eye: visible / hidden on the device (Home can't be hidden) */}
            {!isHome && (
              <button type="button" onClick={() => toggleHidden(screenId)}
                title={hiddenSet.has(String(screenId)) ? 'Hidden on device (click to show' : 'Shown on device) click to hide'}
                style={{
                  background: 'none', border: 'none', cursor: 'pointer',
                  fontSize: 15, lineHeight: 1, flexShrink: 0, padding: '0 2px',
                  opacity: hiddenSet.has(String(screenId)) ? 0.6 : 1,
                }}>
                {hiddenSet.has(String(screenId)) ? '🚫' : '👁️'}
              </button>
            )}

            {/* up/down arrows (alternative to drag on mobile) */}
            {!isHome && (
              <div style={{ display: 'flex', gap: 4, flexShrink: 0 }}>
                <button type="button" onClick={() => moveUp(idx)} disabled={idx === 1}
                  style={{
                    background: 'none', border: `1px solid ${C.border}`, borderRadius: 4,
                    color: idx === 1 ? C.border : C.muted, cursor: idx === 1 ? 'default' : 'pointer',
                    width: 26, height: 26, fontSize: 12, lineHeight: 1,
                  }}>▲</button>
                <button type="button" onClick={() => moveDown(idx)} disabled={idx === order.length - 1}
                  style={{
                    background: 'none', border: `1px solid ${C.border}`, borderRadius: 4,
                    color: idx === order.length - 1 ? C.border : C.muted,
                    cursor: idx === order.length - 1 ? 'default' : 'pointer',
                    width: 26, height: 26, fontSize: 12, lineHeight: 1,
                  }}>▼</button>
              </div>
            )}
          </div>
        )
      })}
      <p style={{ ...s.hint, marginTop: 8 }}>
        Drag rows or use ▲▼ to reorder. Your device picks up the new order on its next check-in.
      </p>
    </div>
  )
}

// ── NFT Pinlist ───────────────────────────────────────────────────────────────

interface PinItem {
  chain:            string
  contract:         string
  tokenId:          string
  bg?:              string   // ordinals only: user-picked background "#rrggbb"
  name?:            string
  image_url?:       string
  collection_name?: string
  floor_price_eth?: number
}

function parseOpenseaUrl(url: string): { chain: string; contract: string; tokenId: string } | null {
  // Matches: https://opensea.io/item/{chain}/{0xcontract}/{tokenId}
  const m = url.match(/opensea\.io\/item\/([^/]+)\/(0x[0-9a-fA-F]{40})\/([0-9]+)/i)
  if (!m) return null
  return { chain: m[1].toLowerCase(), contract: m[2].toLowerCase(), tokenId: m[3] }
}

// Bitcoin Ordinals: satflow/magiceden/ordinals.com URLs or a raw inscription
// id (64 hex chars + "i<n>"). Stored as chain "ord", contract = inscription id.
function parseOrdinal(url: string): { chain: string; contract: string; tokenId: string } | null {
  const m = url.match(/([0-9a-fA-F]{64}i[0-9]+)/)
  if (!m) return null
  return { chain: 'ord', contract: m[1].toLowerCase(), tokenId: '0' }
}

// ── Projects (communities) ────────────────────────────────────────────────────
// The communities a node is part of: tokens (same DexScreener search the
// ticker board uses) or NFT collections (pasted OpenSea / Satflow / ordinals
// link — same parsers as the pinlist below). ONE unified search bar: type a
// ticker OR paste a link. Saved instantly via dedicated Edge Functions
// (add-node-project / remove-node-project / set-favorite-project) — NOT part
// of the main Save button. The ★ favourite is the community shown on block
// tiles next to the winner's name, and feeds the "By Communities" leaderboard.

interface NodeProject {
  project_key: string
  kind:        'token' | 'nft'
  name:        string
  symbol:      string | null
  image_url:   string | null
  chain:       string | null
  is_favorite: boolean
}

interface ProjectResult {
  key:        string
  kind:       'token' | 'nft'
  name:       string
  symbol?:    string
  image_url?: string
  chain?:     string
  ref_url?:   string
  meta?:      string
}

function slugifyProject(t: string): string {
  return t.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, '')
}

function ProjectsEditor({ nodeCode }: { nodeCode: string }) {
  const [projects, setProjects] = useState<NodeProject[]>([])
  const [query,    setQuery]    = useState('')
  const [results,  setResults]  = useState<ProjectResult[]>([])
  const [loading,  setLoading]  = useState(false)
  const [busyKey,  setBusyKey]  = useState<string | null>(null)
  const [error,    setError]    = useState<string | null>(null)
  const [tick,     setTick]     = useState(0)   // bump to reload the saved list

  useEffect(() => {
    supabase
      .from('public_node_projects')
      .select('project_key, kind, name, symbol, image_url, chain, is_favorite')
      .eq('node_code', nodeCode)
      .then(({ data }) => setProjects((data ?? []) as NodeProject[]))
  }, [nodeCode, tick])

  async function search() {
    const q = query.trim()
    if (q.length < 2) return
    setLoading(true)
    setError(null)
    setResults([])
    try {
      const nft = parseOpenseaUrl(q)
      const ord = nft ? null : parseOrdinal(q)
      if (nft) {
        // OpenSea item link → the ITEM's collection is the community
        const res = await callFunction<{ results: { name?: string; image_url?: string; collection_name?: string; error?: string }[] }>(
          'resolve-nft', { items: [`${nft.chain}:${nft.contract}:${nft.tokenId}`] })
        const r = res?.results?.[0]
        if (!r || r.error) throw new Error('Could not resolve that OpenSea link')
        setResults([{
          key:       `nft:${nft.chain}:${nft.contract}`,
          kind:      'nft',
          name:      r.collection_name || r.name || 'NFT collection',
          image_url: r.image_url,
          chain:     nft.chain,
          ref_url:   q,
          meta:      `NFT collection · ${nft.chain}`,
        }])
      } else if (ord) {
        // Satflow / Magic Eden / ordinals.com link (or raw inscription id)
        const res = await callFunction<{ results: { id: string; name?: string | null; collection?: string | null }[] }>(
          'resolve-ordinal', { ids: [ord.contract] })
        const r = res?.results?.[0]
        const coll = r?.collection || null
        setResults([{
          key:       `ord:${coll ? slugifyProject(coll) : ord.contract}`,
          kind:      'nft',
          name:      coll || r?.name || 'Ordinals collection',
          image_url: `https://ordinals.com/content/${ord.contract}`,
          chain:     'ord',
          ref_url:   q,
          meta:      'Ordinals collection',
        }])
      } else {
        // Ticker → same DexScreener search + liquidity filter as the ticker
        // board, but deduped per TOKEN (a community is the token, not a pool).
        const res = await fetch(`https://api.dexscreener.com/latest/dex/search?q=${encodeURIComponent(q)}`)
        if (!res.ok) throw new Error(`DexScreener ${res.status}`)
        const data = await res.json() as { pairs?: Array<{
          pairAddress: string; chainId: string
          liquidity?: { usd?: number }
          baseToken?: { symbol?: string; name?: string; address?: string }
          info?: { imageUrl?: string }
        }> }
        const seen = new Set<string>()
        const mapped: ProjectResult[] = []
        for (const pr of data.pairs ?? []) {
          if ((pr.liquidity?.usd ?? 0) < 1000) continue   // sink dust/scam clones
          const base = (pr.baseToken?.address || pr.pairAddress).toLowerCase()
          const key  = `token:${pr.chainId}:${base}`
          if (seen.has(key)) continue
          seen.add(key)
          mapped.push({
            key,
            kind:      'token',
            name:      pr.baseToken?.name || pr.baseToken?.symbol || '?',
            symbol:    pr.baseToken?.symbol,
            image_url: pr.info?.imageUrl,
            chain:     pr.chainId,
            ref_url:   `https://dexscreener.com/${pr.chainId}/${pr.pairAddress}`,
            meta:      `${pr.chainId} · Liq $${Math.round((pr.liquidity?.usd ?? 0) / 1000)}k`,
          })
          if (mapped.length >= 8) break
        }
        setResults(mapped)
        if (mapped.length === 0) setError('No tokens found. You can also paste an OpenSea or Satflow link.')
      }
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Search failed')
    }
    setLoading(false)
  }

  async function add(r: ProjectResult) {
    setBusyKey(r.key)
    try {
      await callFunction('add-node-project', {
        node_code:   nodeCode,
        project_key: r.key,
        kind:        r.kind,
        name:        r.name,
        symbol:      r.symbol,
        image_url:   r.image_url,
        chain:       r.chain,
        ref_url:     r.ref_url,
      })
      setResults([])
      setQuery('')
      setTick(t => t + 1)
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Could not add project')
    }
    setBusyKey(null)
  }

  async function favorite(p: NodeProject) {
    if (p.is_favorite) return
    setBusyKey(p.project_key)
    try {
      await callFunction('set-favorite-project', { node_code: nodeCode, project_key: p.project_key })
      setTick(t => t + 1)
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Could not set favorite')
    }
    setBusyKey(null)
  }

  async function remove(p: NodeProject) {
    setBusyKey(p.project_key)
    try {
      await callFunction('remove-node-project', { node_code: nodeCode, project_key: p.project_key })
      setTick(t => t + 1)
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Could not remove project')
    }
    setBusyKey(null)
  }

  return (
    <div style={{ marginBottom: 4 }}>
      <label style={s.label}>Projects</label>

      {/* Saved projects */}
      {projects.length > 0 && (
        <div style={{ display: 'flex', flexDirection: 'column', gap: 6, marginBottom: 10 }}>
          {projects.map(p => (
            <div key={p.project_key} style={{
              display: 'flex', alignItems: 'center', gap: 10,
              background: C.surface, border: `1px solid ${p.is_favorite ? `${C.green}55` : C.border}`,
              borderRadius: 8, padding: '7px 10px',
            }}>
              {p.image_url
                ? <img src={p.image_url} alt="" style={{ width: 24, height: 24, borderRadius: 6, objectFit: 'cover', flexShrink: 0, background: '#000' }} />
                : <div style={{ width: 24, height: 24, borderRadius: 6, background: C.border, flexShrink: 0, display: 'flex', alignItems: 'center', justifyContent: 'center', fontSize: 11, color: C.muted }}>{p.kind === 'nft' ? '🖼' : '◆'}</div>}
              <div style={{ flex: 1, minWidth: 0 }}>
                <div style={{ fontSize: 13, fontWeight: 600, color: C.text, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                  {p.name}{p.symbol ? <span style={{ color: C.muted, fontWeight: 400, marginLeft: 6, fontSize: 11 }}>{p.symbol}</span> : null}
                </div>
                <div style={{ fontSize: 10, color: C.muted, marginTop: 1 }}>
                  {p.kind === 'nft' ? 'NFT collection' : 'Token'}{p.chain ? ` · ${p.chain}` : ''}
                </div>
              </div>
              <button type="button" onClick={() => favorite(p)} disabled={busyKey === p.project_key}
                title={p.is_favorite ? 'Favorite — shown on your mined blocks' : 'Make favorite'}
                style={{ background: 'none', border: 'none', cursor: p.is_favorite ? 'default' : 'pointer', fontSize: 15, lineHeight: 1, padding: 2, color: p.is_favorite ? C.yellow : C.muted, flexShrink: 0 }}>
                {p.is_favorite ? '★' : '☆'}
              </button>
              <button type="button" onClick={() => remove(p)} disabled={busyKey === p.project_key}
                style={{ background: 'none', border: 'none', cursor: 'pointer', fontSize: 13, lineHeight: 1, padding: 2, color: C.muted, flexShrink: 0 }}
                aria-label={`Remove ${p.name}`}>✕</button>
            </div>
          ))}
        </div>
      )}

      {/* Unified search: ticker text OR pasted OpenSea/Satflow link */}
      <div style={{ display: 'flex', gap: 8 }}>
        <input
          style={{ ...s.input, flex: 1 }}
          value={query}
          onChange={e => setQuery(e.target.value)}
          onKeyDown={e => { if (e.key === 'Enter') { e.preventDefault(); search() } }}
          placeholder="Search a token, or paste an OpenSea / Satflow link…"
        />
        <button type="button" onClick={search} disabled={loading} style={{
          padding: '0 16px', background: C.surface, color: C.text, border: `1px solid ${C.border}`,
          borderRadius: 8, fontSize: 13, fontWeight: 600, cursor: 'pointer', flexShrink: 0,
        }}>{loading ? '…' : 'Search'}</button>
      </div>

      {/* Results */}
      {results.length > 0 && (
        <div style={{ border: `1px solid ${C.border}`, borderRadius: 8, marginTop: 8, overflow: 'hidden' }}>
          {results.map(r => (
            <div key={r.key} style={{ display: 'flex', alignItems: 'center', gap: 10, padding: '8px 10px', borderBottom: `1px solid ${C.border}`, background: C.card }}>
              {r.image_url && <img src={r.image_url} alt="" style={{ width: 22, height: 22, borderRadius: 5, objectFit: 'cover', flexShrink: 0, background: '#000' }} />}
              <div style={{ flex: 1, minWidth: 0 }}>
                <span style={{ fontWeight: 700, fontSize: 13, color: C.text }}>{r.name}</span>
                {r.symbol && <span style={{ fontSize: 11, color: C.muted, marginLeft: 6 }}>{r.symbol}</span>}
                {r.meta && <span style={{ fontSize: 11, color: C.muted, marginLeft: 6 }}>· {r.meta}</span>}
              </div>
              <button type="button" onClick={() => add(r)} disabled={busyKey === r.key} style={{
                padding: '5px 12px', background: C.green, color: C.onGreen, border: 'none',
                borderRadius: 6, fontSize: 12, fontWeight: 700, cursor: 'pointer', flexShrink: 0,
              }}>{busyKey === r.key ? '…' : '+ Add'}</button>
            </div>
          ))}
        </div>
      )}

      {error && <p style={{ ...s.hint, color: C.red, opacity: 1 }}>{error}</p>}
      <p style={s.hint}>
        Communities you&apos;re part of — tokens or NFT collections. The ★ favorite is shown
        next to your name on mined blocks and counts toward the community leaderboard.
      </p>
    </div>
  )
}

function NftPinlistEditor({ items, onChange }: { items: PinItem[]; onChange: (items: PinItem[]) => void }) {
  const [url,       setUrl]       = useState('')
  const [resolving, setResolving] = useState(false)
  const [error,     setError]     = useState<string | null>(null)

  async function addItem() {
    const trimmed = url.trim()
    if (!trimmed) return
    const parsed = parseOpenseaUrl(trimmed) ?? parseOrdinal(trimmed)
    if (!parsed) {
      setError('Paste an OpenSea URL (https://opensea.io/item/…) or an Ordinals inscription URL/id (satflow.com/ordinal/…, ordinals.com/inscription/…).')
      return
    }
    if (parsed.chain === 'ord') {
      const id = `ord:${parsed.contract}:0`
      if (items.some(i => `${i.chain}:${i.contract}:${i.tokenId}` === id)) { setError('This inscription is already in your list.'); return }
      if (items.length >= 20) { setError('Maximum 20 NFTs in the pinlist.'); return }
      setResolving(true)
      setError(null)
      let name = 'Ordinal'
      let coll: string | undefined
      let floorBtc: number | undefined
      try {
        const res = await callFunction<{ results: { name?: string | null; collection?: string | null; floor_btc?: number | null }[] }>('resolve-ordinal', { ids: [parsed.contract] })
        const r = res?.results?.[0]
        if (r?.name) name = r.name
        if (r?.collection) coll = r.collection
        if (r?.floor_btc) floorBtc = r.floor_btc
      } catch { /* fall back to plain "Ordinal" */ }
      setResolving(false)
      onChange([...items, {
        chain: 'ord', contract: parsed.contract, tokenId: '0',
        name,
        image_url: `https://ordinals.com/content/${parsed.contract}`,
        collection_name: [coll || 'Ordinals', floorBtc ? `${floorBtc.toFixed(3)} ₿` : null]
                         .filter(Boolean).join(' · '),
      }])
      setUrl('')
      return
    }
    const id = `${parsed.chain}:${parsed.contract}:${parsed.tokenId}`
    if (items.some(i => `${i.chain}:${i.contract}:${i.tokenId}` === id)) {
      setError('This NFT is already in your list.')
      return
    }
    if (items.length >= 20) {
      setError('Maximum 20 NFTs in the pinlist.')
      return
    }
    setResolving(true)
    setError(null)
    try {
      const res = await callFunction<{ results: { name?: string; image_url?: string; collection_name?: string; floor_price_eth?: number; error?: string }[] }>('resolve-nft', { items: [id] })
      const result = res?.results?.[0]
      if (!result || result.error) {
        // The DEVICE resolves manual picks with its own OpenSea key, so a failed
        // PREVIEW lookup here (e.g. the resolve-nft function has no OPENSEA_API_KEY
        // set in Supabase → 401) must NOT block adding the pin. Add it with a
        // fallback name; the device fills in the real name/image/floor.
        onChange([...items, {
          chain:    parsed.chain,
          contract: parsed.contract,
          tokenId:  parsed.tokenId,
          name:     `#${parsed.tokenId}`,
        }])
        setUrl('')
        setError('Added. Preview metadata could not be fetched here, but the device will still resolve and display it.')
        return
      }
      onChange([...items, {
        chain:           parsed.chain,
        contract:        parsed.contract,
        tokenId:         parsed.tokenId,
        name:            result.name,
        image_url:       result.image_url,
        collection_name: result.collection_name,
        floor_price_eth: result.floor_price_eth,
      }])
      setUrl('')
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Error resolving NFT.')
    } finally {
      setResolving(false)
    }
  }

  return (
    <div style={{ marginBottom: 4 }}>
      <p style={{ ...s.hint, marginBottom: 10, opacity: 1 }}>
        Paste an OpenSea link (or an Ordinals inscription URL/id) to add NFTs one by one. Picks are ALWAYS shown on the device, alongside your wallet collections. </p>

      {/* URL input + Add button */}
      <div style={{ display: 'flex', gap: 8, marginBottom: 6 }}>
        <input
          style={{ ...s.input, flex: 1 }}
          placeholder="https://opensea.io/item/ethereum/0x…/3968"
          value={url}
          onChange={e => { setUrl(e.target.value); setError(null) }}
          onKeyDown={e => { if (e.key === 'Enter') { e.preventDefault(); addItem() } }}
        />
        <button
          type="button"
          onClick={addItem}
          disabled={resolving || !url.trim()}
          style={{
            padding: '10px 18px', background: C.blue, color: '#fff',
            border: 'none', borderRadius: 8, fontWeight: 'bold', fontSize: 13,
            cursor: resolving || !url.trim() ? 'not-allowed' : 'pointer',
            opacity: resolving || !url.trim() ? 0.45 : 1, flexShrink: 0,
            transition: 'opacity .15s',
          }}
        >{resolving ? '…' : 'Add'}</button>
      </div>

      {error && <p style={{ fontSize: 12, color: C.red, margin: '4px 0 10px' }}>{error}</p>}

      {items.length === 0 ? (
        <p style={{ ...s.hint, textAlign: 'center', padding: '18px 0', opacity: 0.4 }}>
          No NFTs added yet. Paste an OpenSea link above.
        </p>
      ) : (
        <div style={{ display: 'flex', flexDirection: 'column', gap: 8, marginTop: 10 }}>
          {items.map((item, idx) => (
            <div key={idx} style={{
              display: 'flex', alignItems: 'center', gap: 10,
              background: C.surface, border: `1px solid ${C.border}`,
              borderRadius: 8, padding: '10px 12px',
            }}>
              {item.image_url ? (
                /* eslint-disable-next-line @next/next/no-img-element */
                <img
                  src={item.image_url}
                  alt={item.name ?? item.tokenId}
                  style={{ width: 48, height: 48, objectFit: 'cover', borderRadius: 6, flexShrink: 0 }}
                  onError={e => { (e.target as HTMLImageElement).style.display = 'none' }}
                />
              ) : (
                <div style={{ width: 48, height: 48, borderRadius: 6, background: C.card, flexShrink: 0, display: 'flex', alignItems: 'center', justifyContent: 'center', fontSize: 22, color: C.muted }}>
                  🖼
                </div>
              )}
              <div style={{ flex: 1, minWidth: 0 }}>
                <div style={{ fontSize: 13, fontWeight: 600, color: C.text, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                  {item.name ?? `#${item.tokenId}`}
                </div>
                <div style={{ fontSize: 11, color: C.muted, marginTop: 2 }}>
                  {/* Ordinals: no "#0" suffix — their tokenId is a placeholder
                      (pinlist format ord:<inscription>:0); the real number is
                      already part of the name ("NodeMonke #9343"). */}
                  {item.collection_name ?? item.chain}{item.chain !== 'ord' ? ` · #${item.tokenId}` : ''}
                </div>
                {item.floor_price_eth != null && item.floor_price_eth > 0 && (
                  <div style={{ fontSize: 11, color: C.green, marginTop: 1 }}>
                    Ξ{item.floor_price_eth.toFixed(3)} floor
                  </div>
                )}
              </div>
              {item.chain === 'ord' && (
                // Background colour for transparent on-chain art (NodeMonkes
                // et al). Synced to the device, which composites the PNG onto
                // this colour instead of plain black.
                <label title="Artwork background color on the device"
                  style={{ display: 'flex', alignItems: 'center', gap: 5, flexShrink: 0, cursor: 'pointer' }}>
                  <span style={{ fontSize: 10, color: C.muted }}>BG</span>
                  <input
                    type="color"
                    value={item.bg ?? '#000000'}
                    onChange={e => onChange(items.map((it, i) => i === idx ? { ...it, bg: e.target.value.toLowerCase() } : it))}
                    style={{ width: 26, height: 26, padding: 0, border: `1px solid ${C.border}`, borderRadius: 6, background: 'none', cursor: 'pointer' }}
                  />
                </label>
              )}
              <button
                type="button"
                onClick={() => onChange(items.filter((_, i) => i !== idx))}
                style={{ background: 'none', border: 'none', color: C.muted, cursor: 'pointer', fontSize: 16, padding: '4px', flexShrink: 0, lineHeight: 1 }}
                aria-label="Remove NFT"
              >✕</button>
            </div>
          ))}
          <p style={{ ...s.hint, marginTop: 2 }}>
            {items.length}/20 NFTs · Picks merge with the wallet scan, remove one and that
            grid cell returns to the wallet collections.
          </p>
        </div>
      )}
    </div>
  )
}

function Centered({ children }: { children: React.ReactNode }) {
  return (
    <div style={{ minHeight: '100vh', background: C.bg, display: 'flex', alignItems: 'center', justifyContent: 'center', fontFamily: 'system-ui, sans-serif' }}>
      <p style={{ color: C.muted, padding: 24, textAlign: 'center' }}>{children}</p>
    </div>
  )
}

// ── Styles ────────────────────────────────────────────────────────────────────
const s: Record<string, React.CSSProperties> = {
  root:    { minHeight: '100vh', background: C.bg, color: C.text, fontFamily: 'system-ui, -apple-system, sans-serif' },
  content: { maxWidth: 560, margin: '0 auto', padding: '28px 20px 80px' },

  header: {
    display: 'flex', alignItems: 'center', justifyContent: 'space-between',
    padding: '0 20px', height: 56, borderBottom: `1px solid ${C.border}`,
    position: 'sticky', top: 0, background: 'rgba(0,0,0,0.92)',
    backdropFilter: 'blur(12px)', zIndex: 10,
  },
  back:      { color: C.muted, textDecoration: 'none', fontSize: 14, flex: '1 0 0' },
  logoWrap:  { display: 'flex', alignItems: 'center', gap: 10 },
  logo:      { fontSize: 16, fontWeight: 'bold', color: C.text, display: 'flex', gap: 6, alignItems: 'center', flex: '0 0 auto', justifyContent: 'center' },
  badge:     { background: C.green, color: C.onGreen, fontSize: 10, fontWeight: 'bold', padding: '2px 6px', borderRadius: 4 },
  genBadge:  { fontSize: 14 },
  setupLink: { color: C.muted, fontSize: 13, textDecoration: 'none', border: `1px solid ${C.border}`, padding: '5px 10px', borderRadius: 6, flex: '0 0 auto' },
  statsBar:  { display: 'flex', justifyContent: 'center', alignItems: 'center', gap: 36, padding: '12px 20px', borderBottom: `1px solid ${C.border}`, background: C.card },

  section: {
    borderLeft: `3px solid ${C.green}`, paddingLeft: 16,
    marginBottom: 28,
  },
  sectionTitle: { fontSize: 16, fontWeight: 'bold', letterSpacing: 1.2, textTransform: 'uppercase', color: '#ffffff', marginBottom: 16, marginTop: 0 },

  label: { display: 'block', fontSize: 13, color: C.muted, marginBottom: 6, fontWeight: 500 },
  hint:  { fontSize: 12, color: C.muted, marginTop: 5, lineHeight: 1.5, opacity: 0.7 },
  input: {
    width: '100%', padding: '10px 12px', background: C.surface, color: C.text,
    border: `1px solid ${C.border}`, borderRadius: 8, fontSize: 14,
    boxSizing: 'border-box', outline: 'none',
    transition: 'border-color 0.15s',
  },
  atSign:       { position: 'absolute', left: 10, top: '50%', transform: 'translateY(-50%)', color: C.muted, fontSize: 14, pointerEvents: 'none' },
  checkboxLabel: { display: 'flex', alignItems: 'center', gap: 8, fontSize: 14, color: C.muted, cursor: 'pointer' },

  primaryBtn: {
    width: '100%', padding: '13px 0', background: C.green, color: C.onGreen,
    border: 'none', borderRadius: 10, fontWeight: 'bold', fontSize: 15, cursor: 'pointer',
    letterSpacing: 0.3, marginTop: 4,
  },
  outlineBtn: {
    width: '100%', padding: '12px 0', background: 'transparent', color: C.green,
    border: `1px solid ${C.green}`, borderRadius: 10, fontWeight: 'bold', fontSize: 14,
    cursor: 'pointer', marginTop: 4,
  },
  msg:      { fontSize: 12, marginTop: 10 },
  bodyText: { color: C.muted, fontSize: 15, lineHeight: 1.7, marginTop: 0 },
}

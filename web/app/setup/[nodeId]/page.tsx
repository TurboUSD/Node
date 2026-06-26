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
import { supabase, callFunction } from '@/lib/supabase'
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
  // NFT Gallery (optional — requires DB migration)
  nft_wallet_address?:    string | null
  nft_grid_size?:         1 | 4 | 9
  nft_carousel_enabled?:  boolean
  nft_slideshow_secs?:    number
  // Screen order (optional — requires DB migration)
  // Comma-separated ScreenId integers, e.g. "0,1,2,3,4,5,6". Position 0 is always Home.
  screen_order?:          string | null
  // Manual NFT pinlist (optional — requires DB migration)
  // Comma-separated "chain:contract:tokenId" items, max 20. Takes priority over nft_wallet_address on device.
  nft_pinlist?:           string | null
}

interface NodeStats {
  uptime_pct:    number
  blocks_won:    number
  windows_online: number
  total_tusd_earned: number
}

// ISO 3166-1 alpha-2 country list (abbreviated — add more as needed)
const COUNTRIES: [string, string][] = [
  ['', 'Select country…'],
  ['AR', '🇦🇷 Argentina'], ['AU', '🇦🇺 Australia'], ['AT', '🇦🇹 Austria'],
  ['BE', '🇧🇪 Belgium'], ['BR', '🇧🇷 Brazil'], ['CA', '🇨🇦 Canada'],
  ['CL', '🇨🇱 Chile'], ['CO', '🇨🇴 Colombia'], ['HR', '🇭🇷 Croatia'],
  ['CZ', '🇨🇿 Czech Republic'], ['DK', '🇩🇰 Denmark'], ['EG', '🇪🇬 Egypt'],
  ['FI', '🇫🇮 Finland'], ['FR', '🇫🇷 France'], ['DE', '🇩🇪 Germany'],
  ['GR', '🇬🇷 Greece'], ['HK', '🇭🇰 Hong Kong'], ['HU', '🇭🇺 Hungary'],
  ['IN', '🇮🇳 India'], ['ID', '🇮🇩 Indonesia'], ['IE', '🇮🇪 Ireland'],
  ['IL', '🇮🇱 Israel'], ['IT', '🇮🇹 Italy'], ['JP', '🇯🇵 Japan'],
  ['KR', '🇰🇷 South Korea'], ['MX', '🇲🇽 Mexico'], ['NL', '🇳🇱 Netherlands'],
  ['NZ', '🇳🇿 New Zealand'], ['NG', '🇳🇬 Nigeria'], ['NO', '🇳🇴 Norway'],
  ['PL', '🇵🇱 Poland'], ['PT', '🇵🇹 Portugal'], ['RO', '🇷🇴 Romania'],
  ['RU', '🇷🇺 Russia'], ['SA', '🇸🇦 Saudi Arabia'], ['SG', '🇸🇬 Singapore'],
  ['ZA', '🇿🇦 South Africa'], ['ES', '🇪🇸 Spain'], ['SE', '🇸🇪 Sweden'],
  ['CH', '🇨🇭 Switzerland'], ['TW', '🇹🇼 Taiwan'], ['TH', '🇹🇭 Thailand'],
  ['TR', '🇹🇷 Turkey'], ['UA', '🇺🇦 Ukraine'], ['AE', '🇦🇪 UAE'],
  ['GB', '🇬🇧 United Kingdom'], ['US', '🇺🇸 United States'], ['VE', '🇻🇪 Venezuela'],
]

// ── Page ─────────────────────────────────────────────────────────────────────
export default function NodeSetupPage({ params }: { params: { nodeId: string } }) {
  const nodeCode = params.nodeId.toUpperCase()
  const [node,        setNode]        = useState<NodeConfig | null>(null)
  const [stats,       setStats]       = useState<NodeStats | null>(null)
  const [loading,     setLoading]     = useState(true)
  const [saving,      setSaving]      = useState(false)
  const [saveMsg,     setSaveMsg]     = useState<{ text: string; ok: boolean } | null>(null)

  const [tweetUrl,    setTweetUrl]    = useState('')
  const [verifyBusy,  setVerifyBusy]  = useState(false)
  const [verifyMsg,   setVerifyMsg]   = useState<{ text: string; ok: boolean } | null>(null)

  // NFT Gallery: tab selector + manual pinlist
  const [nftTab,   setNftTab]   = useState<'wallet' | 'manual'>('wallet')
  const [pinItems, setPinItems] = useState<PinItem[]>([])
  const pinlistInitRef = useRef(false)

  useEffect(() => {
    // Remember this node code so the network page can show "My Node →" instead of "Setup →"
    localStorage.setItem('turbousd_node_code', nodeCode)

    supabase
      .from('nodes')
      .select('node_code, display_name, bio, wallet_address, twitter_handle, country, city, is_verified, is_genesis, temp_unit, date_format, time_format, alarm_hour, alarm_minute, alarm_enabled, alarm_volume, screen_brightness, screen_always_on, screen_timeout_mins')
      .eq('node_code', nodeCode)
      .maybeSingle()
      .then(({ data, error }) => {
        if (!error && data) setNode(data as NodeConfig)
        setLoading(false)
      })
    // Fetch stats from the directory view (includes uptime_pct)
    supabase
      .from('public_node_directory')
      .select('uptime_pct, blocks_won, windows_online, total_tusd_earned')
      .eq('node_code', nodeCode)
      .maybeSingle()
      .then(({ data }) => {
        if (data) setStats(data as NodeStats)
      })
  }, [nodeCode])

  // Parse nft_pinlist from DB into PinItem[] — once, when node first loads
  useEffect(() => {
    if (!node || pinlistInitRef.current) return
    pinlistInitRef.current = true
    if (!node.nft_pinlist) return
    const items: PinItem[] = node.nft_pinlist.split(',').flatMap(raw => {
      const firstColon  = raw.indexOf(':')
      const secondColon = raw.indexOf(':', firstColon + 1)
      if (firstColon < 1 || secondColon < 1) return []
      return [{
        chain:    raw.slice(0, firstColon),
        contract: raw.slice(firstColon + 1, secondColon),
        tokenId:  raw.slice(secondColon + 1),
        // name / image_url not stored in DB — shown as plain IDs until user re-adds via UI
      }]
    })
    if (items.length > 0) {
      setPinItems(items)
      setNftTab('manual')
    }
  }, [node])

  async function handleSave(e: React.FormEvent) {
    e.preventDefault()
    if (!node) return
    setSaving(true)
    setSaveMsg(null)
    try {
      await callFunction('update-node-config', {
        node_code:             nodeCode,
        display_name:          node.display_name,
        bio:                   node.bio,
        wallet_address:        node.wallet_address,
        twitter_handle:        node.twitter_handle,
        country:               node.country,
        city:                  node.city,
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
        nft_wallet_address:    node.nft_wallet_address,
        nft_grid_size:         node.nft_grid_size,
        nft_carousel_enabled:  node.nft_carousel_enabled,
        nft_slideshow_secs:    node.nft_slideshow_secs,
        // Serialize pinlist: active if items exist, null to clear (falls back to wallet mode on device)
        nft_pinlist:           pinItems.length > 0
                                 ? pinItems.map(i => `${i.chain}:${i.contract}:${i.tokenId}`).join(',')
                                 : null,
        screen_order:          node.screen_order ?? undefined,
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
      setVerifyMsg({ text: 'Submitted! We review manually — usually within a couple of days.', ok: true })
    } catch (err) {
      setVerifyMsg({ text: err instanceof Error ? err.message : 'Something went wrong.', ok: false })
    } finally {
      setVerifyBusy(false)
    }
  }

  if (loading) return <Centered>Loading node {nodeCode}…</Centered>
  if (!node)   return <NotFound nodeCode={nodeCode} />

  const isProfileComplete = !!(node.wallet_address && node.display_name && node.country)

  return (
    <div style={s.root}>
      <Header nodeCode={node.node_code} isVerified={node.is_verified} isGenesis={node.is_genesis} stats={stats} />

      <div style={s.content}>
        <form onSubmit={handleSave}>

          {/* ── Profile ── */}
          <Section title="Profile" accent={C.green}>
            {!isProfileComplete && (
              <Banner color={C.yellow}>
                Complete your profile to start receiving ₸USD rewards — wallet address is required for payouts.
              </Banner>
            )}
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
            <Row>
              <Field label="Country">
                <select style={s.input}
                  value={node.country ?? ''}
                  onChange={e => setNode({ ...node, country: e.target.value || null })}
                >
                  {COUNTRIES.map(([code, name]) => (
                    <option key={code} value={code}>{name}</option>
                  ))}
                </select>
              </Field>
              <Field label="City (optional)">
                <input style={s.input} maxLength={64} placeholder="Madrid, London, …"
                  value={node.city ?? ''}
                  onChange={e => setNode({ ...node, city: e.target.value || null })}
                />
              </Field>
            </Row>
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
              hint="Without the @ — used for reward notifications and the verified badge."
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

            {/* Turn off after — only visible when always-on is OFF */}
            {!(node.screen_always_on ?? true) && (
              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', padding: '10px 0', borderTop: `1px solid ${C.border}` }}>
                <span style={{ fontSize: 14, color: C.text }}>Turn off after</span>
                <div style={{ display: 'flex', gap: 6 }}>
                  {([1, 5, 10, 30] as const).map(mins => (
                    <button
                      key={mins}
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

            <ToggleRow label="Temperature" value={node.temp_unit}
              options={[['C', '°C'], ['F', '°F']]}
              onChange={v => setNode({ ...node, temp_unit: v as 'C' | 'F' })}
            />
            <ToggleRow label="Date format" value={node.date_format}
              options={[['DD/MM', 'DD/MM'], ['MM/DD', 'MM/DD']]}
              onChange={v => setNode({ ...node, date_format: v as 'DD/MM' | 'MM/DD' })}
            />
            <ToggleRow label="Time format" value={node.time_format}
              options={[['24H', '24h'], ['AMPM', 'AM/PM']]}
              onChange={v => setNode({ ...node, time_format: v as '24H' | 'AMPM' })}
            />
          </Section>

          <button type="submit" disabled={saving} style={s.primaryBtn}>
            {saving ? 'Saving…' : 'Save changes'}
          </button>
          {saveMsg && (
            <p style={{ ...s.msg, color: saveMsg.ok ? C.green : C.red }}>{saveMsg.text}</p>
          )}
        </form>

        {/* ── Token screener ── */}
        <TickerBoard nodeCode={nodeCode} isOwner={true} />

        {/* ── NFT Gallery ── */}
        <Section title="NFT Gallery" accent={C.blue}>
          <p style={s.bodyText}>
            Your device can auto-detect NFTs from a wallet, or show a hand-picked list of
            specific NFTs. Manual picks take priority when active.
            Data is fetched from OpenSea and cached 30 minutes on device.
          </p>

          {/* Mode tabs */}
          <div style={{ display: 'flex', border: `1px solid ${C.border}`, borderRadius: 8, overflow: 'hidden', marginBottom: 16 }}>
            {(['wallet', 'manual'] as const).map((tab, i) => (
              <button
                key={tab}
                type="button"
                onClick={() => setNftTab(tab)}
                style={{
                  flex: 1, padding: '9px 0', fontSize: 13, fontWeight: 600,
                  background: nftTab === tab ? C.blue : 'transparent',
                  color:      nftTab === tab ? '#fff' : C.muted,
                  border: 'none', cursor: 'pointer',
                  borderLeft: i > 0 ? `1px solid ${C.border}` : 'none',
                  transition: 'background .15s',
                }}
              >
                {tab === 'wallet' ? 'By Wallet' : `Manual Picks${pinItems.length > 0 ? ` (${pinItems.length})` : ''}`}
              </button>
            ))}
          </div>

          {nftTab === 'wallet' ? (
            <Field label="NFT wallet address" hint="EVM address (0x…) to auto-detect NFTs from. Can differ from your reward wallet. Spam NFTs (floor price = 0) are filtered automatically.">
              <input style={s.input} placeholder="0x… (defaults to reward wallet if empty)" maxLength={42}
                value={node.nft_wallet_address ?? ''}
                onChange={e => setNode({ ...node, nft_wallet_address: e.target.value || null })}
              />
            </Field>
          ) : (
            <NftPinlistEditor items={pinItems} onChange={setPinItems} />
          )}

          {/* Display settings apply to both modes */}
          <div style={{ marginTop: 4 }}>
            <ToggleRow label="Grid size" value={String(node.nft_grid_size ?? 9)}
              options={[['1', '1×1'], ['4', '2×2'], ['9', '3×3']]}
              onChange={v => setNode({ ...node, nft_grid_size: Number(v) as 1 | 4 | 9 })}
            />
            <div style={{ display: 'flex', gap: 16, alignItems: 'center', marginTop: 8, marginBottom: 10 }}>
              <label style={s.checkboxLabel}>
                <input type="checkbox"
                  checked={node.nft_carousel_enabled ?? true}
                  onChange={e => setNode({ ...node, nft_carousel_enabled: e.target.checked })}
                  style={{ accentColor: C.green }}
                />
                Auto-carousel (cycle NFTs per cell)
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
            onChange={order => setNode({ ...node, screen_order: order })}
          />
        </Section>

        {/* ── Verification ── */}
        <Section title="Verified badge" accent={C.green}>
          {node.is_verified ? (
            <p style={{ color: C.green, fontSize: 14 }}>✓ This node is verified. Your ₸USD rewards will be paid to the configured wallet.</p>
          ) : (
            <>
              <p style={s.bodyText}>
                To get the <strong style={{ color: C.green }}>✓</strong> badge and start receiving rewards:
                post a short video on X tagging <strong style={{ color: C.green }}>@turbousd</strong> showing
                your device screen with your node name — then submit the X link below.
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
      </div>
    </div>
  )
}

// ── Sub-components ────────────────────────────────────────────────────────────

function Header({ nodeCode, isVerified, isGenesis, stats }: {
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
          TurboUSD Node {nodeCode}
          {isVerified && <span style={s.badge}>✓</span>}
          {isGenesis  && <span style={s.genBadge}>🎖</span>}
        </span>
        <a href="/setup" style={s.setupLink}>Flash new device</a>
      </header>
      {stats && (
        <div style={s.statsBar}>
          <StatChip label="Uptime"   value={`⚡ ${stats.uptime_pct}%`}  color={stats.uptime_pct >= 90 ? '#43e397' : stats.uptime_pct >= 60 ? '#ffcf72' : '#6e7280'} />
          <StatChip label="Earned"   value={`₸${stats.total_tusd_earned.toFixed(2)}`} color="#43e397" />
          <StatChip label="Blocks"   value={String(stats.blocks_won)}   color="#5b8dee" />
        </div>
      )}
    </>
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
          Check the code shown on your device's home screen.
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
  0: 'Home',        // CLOCK — always fixed first
  1: 'TurboStats',  // TURBO_STATS
  2: 'Debt',        // DEBT
  3: 'Inflation',   // INFLATION_GAME
  4: 'My Node',     // NODE_NETWORK
  5: 'NFT',         // NFT
  6: 'Tickers',     // TICKERS
}

const DEFAULT_ORDER = [0, 1, 2, 3, 4, 5, 6]

function parseOrder(raw: string | null): number[] {
  if (!raw) return DEFAULT_ORDER
  const parts = raw.split(',').map(s => parseInt(s.trim(), 10))
  if (parts.length === 7 && parts.every(n => n >= 0 && n <= 6) && new Set(parts).size === 7 && parts[0] === 0)
    return parts
  return DEFAULT_ORDER
}

function ScreenOrderSection({ value, onChange }: { value: string | null; onChange: (v: string) => void }) {
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
            <span style={{ flex: 1, fontSize: 14, fontWeight: 600, color: isHome ? C.muted : C.text }}>
              {SCREEN_LABELS[screenId] ?? `Screen ${screenId}`}
            </span>

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

function NftPinlistEditor({ items, onChange }: { items: PinItem[]; onChange: (items: PinItem[]) => void }) {
  const [url,       setUrl]       = useState('')
  const [resolving, setResolving] = useState(false)
  const [error,     setError]     = useState<string | null>(null)

  async function addItem() {
    const trimmed = url.trim()
    if (!trimmed) return
    const parsed = parseOpenseaUrl(trimmed)
    if (!parsed) {
      setError('Paste a valid OpenSea URL — e.g. https://opensea.io/item/ethereum/0x…/3968')
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
        setError(result?.error ?? 'Could not fetch NFT metadata from OpenSea.')
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
        Paste an OpenSea link to add NFTs one by one. The device will only show these specific NFTs (pinlist takes priority over wallet).
      </p>

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
                  {item.collection_name ?? item.chain} · #{item.tokenId}
                </div>
                {item.floor_price_eth != null && item.floor_price_eth > 0 && (
                  <div style={{ fontSize: 11, color: C.green, marginTop: 1 }}>
                    Ξ{item.floor_price_eth.toFixed(3)} floor
                  </div>
                )}
              </div>
              <button
                type="button"
                onClick={() => onChange(items.filter((_, i) => i !== idx))}
                style={{ background: 'none', border: 'none', color: C.muted, cursor: 'pointer', fontSize: 16, padding: '4px', flexShrink: 0, lineHeight: 1 }}
                aria-label="Remove NFT"
              >✕</button>
            </div>
          ))}
          <p style={{ ...s.hint, marginTop: 2 }}>
            {items.length}/20 NFTs · Remove all items to revert to wallet mode.
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
  back:      { color: C.muted, textDecoration: 'none', fontSize: 14 },
  logoWrap:  { display: 'flex', alignItems: 'center', gap: 10 },
  logo:      { fontSize: 16, fontWeight: 'bold', color: C.text, display: 'flex', gap: 6, alignItems: 'center' },
  badge:     { background: C.green, color: C.onGreen, fontSize: 10, fontWeight: 'bold', padding: '2px 6px', borderRadius: 4 },
  genBadge:  { fontSize: 14 },
  setupLink: { color: C.muted, fontSize: 13, textDecoration: 'none', border: `1px solid ${C.border}`, padding: '5px 10px', borderRadius: 6 },
  statsBar:  { display: 'flex', justifyContent: 'center', gap: 36, padding: '14px 20px 10px', borderBottom: `1px solid ${C.border}`, background: C.card },

  section: {
    borderLeft: `3px solid ${C.green}`, paddingLeft: 16,
    marginBottom: 28,
  },
  sectionTitle: { fontSize: 11, fontWeight: 'bold', letterSpacing: 1.5, textTransform: 'uppercase', color: C.muted, marginBottom: 16, marginTop: 0 },

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

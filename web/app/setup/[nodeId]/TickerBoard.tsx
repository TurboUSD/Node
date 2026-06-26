'use client'

// TickerBoard — token screener widget for a node's setup page.
//
// Compact view (list): logo · name · FDV · 24h change · mini sparkline · [expand] [remove]
// Expanded view (one at a time): logo · name · chain · FDV (big) · price (small)
//   · 24h change · candlestick chart with timeframe selector · [collapse] [remove]
//
// Data:
//   - Ticker list   : Supabase node_tickers (via node_ticker_config view)
//   - Live prices   : DexScreener pairs API (client-side, CORS allowed)
//   - OHLCV charts  : GeckoTerminal free API (client-side, CORS allowed)
//   - Token search  : /functions/v1/search-tokens Edge Function

import { useEffect, useState, useCallback } from 'react'
import { supabase, callFunction } from '@/lib/supabase'

// ── Brand ─────────────────────────────────────────────────────────────────────
const C = {
  green:   '#43e397',
  red:     '#ff6b6b',
  blue:    '#5b8dee',
  yellow:  '#ffcf72',
  bg:      '#000000',
  card:    '#0c0c0c',
  surface: '#141414',
  border:  '#1c1c1c',
  text:    '#e8e8e8',
  muted:   '#6e7280',
}

// ── Types ─────────────────────────────────────────────────────────────────────
interface StoredTicker {
  pool_address:  string
  chain_id:      string
  base_symbol:   string
  base_name:     string
  quote_symbol:  string | null
  display_order: number
}

interface LiveData {
  priceUsd:    number
  change24h:   number   // percent
  fdv:         number | null
  imageUrl:    string | null
  pairUrl:     string | null
}

interface Candle {
  time:   number   // unix ms
  open:   number
  high:   number
  low:    number
  close:  number
}

interface SearchResult {
  pairAddress:   string
  chainId:       string
  baseSymbol:    string
  baseName:      string
  baseAddress:   string
  quoteSymbol:   string
  priceUsd:      number
  liquidityUsd:  number
  volume24h:     number
  priceChange24h: number
  fdv:           number | null
}

// ── Helpers ───────────────────────────────────────────────────────────────────
function fmtCap(n: number | null): string {
  if (n == null) return '—'
  if (n >= 1e9) return `$${(n / 1e9).toFixed(2)}B`
  if (n >= 1e6) return `$${(n / 1e6).toFixed(2)}M`
  if (n >= 1e3) return `$${(n / 1e3).toFixed(2)}K`
  return `$${n.toFixed(2)}`
}

function fmtPrice(n: number): string {
  if (n < 0.00001) return `$${n.toExponential(2)}`
  if (n < 0.001)   return `$${n.toFixed(6)}`
  if (n < 0.1)     return `$${n.toFixed(4)}`
  if (n < 10)      return `$${n.toFixed(3)}`
  return `$${n.toLocaleString('en-US', { maximumFractionDigits: 2 })}`
}

// DexScreener chainId → GeckoTerminal network slug
const CHAIN_TO_GT: Record<string, string> = {
  ethereum: 'eth',
  base:     'base',
  bsc:      'bsc',
  polygon:  'polygon_pos',
  arbitrum: 'arbitrum',
  optimism: 'optimism',
  avalanche: 'avax',
  solana:   'solana',
}

const TF_CONFIG: Record<string, { tf: string; agg: number; limit: number }> = {
  '1D':  { tf: 'hour', agg: 1,  limit: 24  },
  '1W':  { tf: 'hour', agg: 4,  limit: 42  },
  '1M':  { tf: 'day',  agg: 1,  limit: 30  },
  '1Y':  { tf: 'day',  agg: 7,  limit: 53  },
  'ALL': { tf: 'day',  agg: 30, limit: 60  },
}

async function fetchLiveData(chainId: string, poolAddress: string): Promise<LiveData | null> {
  try {
    const res = await fetch(`https://api.dexscreener.com/latest/dex/pairs/${chainId}/${poolAddress}`)
    const json = await res.json()
    const pair = json?.pairs?.[0]
    if (!pair) return null
    return {
      priceUsd:  parseFloat(pair.priceUsd ?? '0'),
      change24h: pair.priceChange?.h24 ?? 0,
      fdv:       pair.fdv ?? null,
      imageUrl:  pair.info?.imageUrl ?? null,
      pairUrl:   pair.url ?? null,
    }
  } catch { return null }
}

async function fetchOHLCV(chainId: string, poolAddress: string, tfKey: string): Promise<Candle[]> {
  const network = CHAIN_TO_GT[chainId] ?? chainId
  const { tf, agg, limit } = TF_CONFIG[tfKey]
  const url = `https://api.geckoterminal.com/api/v2/networks/${network}/pools/${poolAddress}/ohlcv/${tf}?aggregate=${agg}&limit=${limit}&currency=usd&token=base`
  try {
    const res  = await fetch(url, { headers: { Accept: 'application/json' } })
    const json = await res.json()
    const raw: number[][] = json?.data?.attributes?.ohlcv_list ?? []
    return raw
      .map(([t, o, h, l, c]) => ({ time: t * 1000, open: o, high: h, low: l, close: c }))
      .sort((a, b) => a.time - b.time)
  } catch { return [] }
}

// ── Sub-components ─────────────────────────────────────────────────────────────

function Sparkline({ candles, isUp }: { candles: Candle[]; isUp: boolean }) {
  if (candles.length < 2) return <div style={{ width: 72, height: 32 }} />
  const closes = candles.map(c => c.close)
  const min = Math.min(...closes)
  const max = Math.max(...closes)
  const range = max - min || 1
  const W = 72, H = 32
  const pts = closes.map((v, i) => {
    const x = (i / (closes.length - 1)) * W
    const y = H - ((v - min) / range) * H
    return `${x.toFixed(1)},${y.toFixed(1)}`
  }).join(' ')
  return (
    <svg width={W} height={H} viewBox={`0 0 ${W} ${H}`} style={{ flexShrink: 0 }}>
      <polyline points={pts} fill="none" stroke={isUp ? C.green : C.red} strokeWidth={1.5} strokeLinejoin="round" />
    </svg>
  )
}

function CandlestickChart({ candles, width = 360, height = 160 }: { candles: Candle[]; width?: number; height?: number }) {
  if (candles.length === 0) return <div style={{ height, display: 'flex', alignItems: 'center', justifyContent: 'center', color: C.muted, fontSize: 12 }}>No data</div>

  const AXIS_W = 52, AXIS_H = 20
  const cW = width - AXIS_W, cH = height - AXIS_H
  const minL  = Math.min(...candles.map(c => c.low))
  const maxH  = Math.max(...candles.map(c => c.high))
  const range = maxH - minL || 1

  const toY = (v: number) => ((maxH - v) / range) * cH
  const bW   = Math.max(2, (cW / candles.length) * 0.65)

  // Y grid labels
  const gridVals = [maxH, (maxH + minL) / 2, minL]

  return (
    <svg width={width} height={height} style={{ display: 'block' }}>
      {/* Grid lines */}
      {gridVals.map((v, i) => {
        const y = toY(v)
        return (
          <g key={i}>
            <line x1={0} y1={y} x2={cW} y2={y} stroke={C.border} strokeWidth={1} strokeDasharray="3,3" />
            <text x={cW + 4} y={y + 4} fill={C.muted} fontSize={9} fontFamily="monospace">
              {fmtPrice(v).replace('$', '')}
            </text>
          </g>
        )
      })}

      {/* Candles */}
      {candles.map((c, i) => {
        const x   = ((i + 0.5) / candles.length) * cW
        const up  = c.close >= c.open
        const col = up ? C.green : C.red
        const bT  = toY(Math.max(c.open, c.close))
        const bB  = toY(Math.min(c.open, c.close))
        const bH  = Math.max(1, bB - bT)
        return (
          <g key={i}>
            <line x1={x} y1={toY(c.high)} x2={x} y2={toY(c.low)} stroke={col} strokeWidth={1} />
            <rect x={x - bW / 2} y={bT} width={bW} height={bH} fill={up ? col : 'transparent'} stroke={col} strokeWidth={1} />
          </g>
        )
      })}

      {/* Time axis: first and last label */}
      {candles.length > 0 && (() => {
        const fmt = (ms: number) => new Date(ms).toLocaleDateString('en-GB', { day: 'numeric', month: 'short' })
        const x0  = (0.5 / candles.length) * cW
        const x1  = ((candles.length - 0.5) / candles.length) * cW
        return (
          <>
            <text x={x0} y={height - 4} fill={C.muted} fontSize={9} textAnchor="middle" fontFamily="system-ui">{fmt(candles[0].time)}</text>
            <text x={x1} y={height - 4} fill={C.muted} fontSize={9} textAnchor="middle" fontFamily="system-ui">{fmt(candles[candles.length - 1].time)}</text>
          </>
        )
      })()}
    </svg>
  )
}

// ── Compact card ──────────────────────────────────────────────────────────────
function CompactCard({
  ticker, live, sparkCandles, onExpand, onRemove, isOwner,
}: {
  ticker:       StoredTicker
  live:         LiveData | null
  sparkCandles: Candle[]
  onExpand:     () => void
  onRemove:     () => void
  isOwner:      boolean
}) {
  const isUp   = (live?.change24h ?? 0) >= 0
  const chgCol = isUp ? C.green : C.red
  const chgStr = live ? `${isUp ? '+' : ''}${live.change24h.toFixed(2)}%` : '…'

  return (
    <div style={s.compactCard}>
      {/* Logo */}
      <div style={s.logoWrap}>
        {live?.imageUrl
          ? <img src={live.imageUrl} alt={ticker.base_symbol} style={{ width: 36, height: 36, borderRadius: '50%', objectFit: 'cover' }} />
          : <div style={s.logoFallback}>{ticker.base_symbol.slice(0, 2)}</div>
        }
      </div>

      {/* Text */}
      <div style={{ flex: 1, minWidth: 0 }}>
        <div style={{ fontSize: 14, fontWeight: 700, color: C.text, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
          {ticker.base_name}
        </div>
        <div style={{ display: 'flex', gap: 8, alignItems: 'center', marginTop: 3, flexWrap: 'wrap' }}>
          <span style={{ fontSize: 11, color: C.muted, fontWeight: 600 }}>{ticker.base_symbol}</span>
          <span style={{ fontSize: 11, color: C.muted }}>·</span>
          <span style={{ fontSize: 11, color: C.muted }}>{fmtCap(live?.fdv ?? null)}</span>
          {live && (
            <>
              <span style={{ fontSize: 11, color: C.muted }}>·</span>
              <span style={{ fontSize: 11, fontWeight: 700, color: chgCol }}>{chgStr}</span>
            </>
          )}
        </div>
      </div>

      {/* Sparkline */}
      <Sparkline candles={sparkCandles} isUp={isUp} />

      {/* Buttons */}
      <div style={{ display: 'flex', gap: 6, flexShrink: 0 }}>
        <button style={s.iconBtn} onClick={onExpand} title="Expand">⤢</button>
        {isOwner && <button style={{ ...s.iconBtn, color: C.muted }} onClick={e => { e.stopPropagation(); onRemove() }} title="Remove">✕</button>}
      </div>
    </div>
  )
}

// ── Expanded card ─────────────────────────────────────────────────────────────
function ExpandedCard({
  ticker, live, candles, loadingCandles, timeframe, onTimeframe, onCollapse, onRemove, isOwner, chartWidth,
}: {
  ticker:         StoredTicker
  live:           LiveData | null
  candles:        Candle[]
  loadingCandles: boolean
  timeframe:      string
  onTimeframe:    (tf: string) => void
  onCollapse:     () => void
  onRemove:       () => void
  isOwner:        boolean
  chartWidth:     number
}) {
  const isUp   = (live?.change24h ?? 0) >= 0
  const chgCol = isUp ? C.green : C.red

  return (
    <div style={s.expandedCard}>
      {/* Header row */}
      <div style={{ display: 'flex', alignItems: 'flex-start', gap: 12, marginBottom: 16 }}>
        {live?.imageUrl
          ? <img src={live.imageUrl} alt={ticker.base_symbol} style={{ width: 44, height: 44, borderRadius: '50%', objectFit: 'cover', flexShrink: 0 }} />
          : <div style={{ ...s.logoFallback, width: 44, height: 44, fontSize: 16 }}>{ticker.base_symbol.slice(0, 2)}</div>
        }
        <div style={{ flex: 1, minWidth: 0 }}>
          <div style={{ fontSize: 16, fontWeight: 700, color: C.text }}>{ticker.base_name}</div>
          <div style={{ fontSize: 11, color: C.muted, marginTop: 2 }}>
            {ticker.base_symbol}{ticker.quote_symbol ? `/${ticker.quote_symbol}` : ''} · {ticker.chain_id.toUpperCase()}
          </div>
        </div>
        <div style={{ display: 'flex', gap: 6 }}>
          <button style={s.iconBtn} onClick={onCollapse} title="Collapse">⤡</button>
          {isOwner && <button style={{ ...s.iconBtn, color: C.muted }} onClick={onRemove} title="Remove">✕</button>}
        </div>
      </div>

      {/* Price block */}
      <div style={{ display: 'flex', alignItems: 'flex-end', gap: 14, marginBottom: 10, flexWrap: 'wrap' }}>
        <div>
          <div style={{ fontSize: 11, color: C.muted, marginBottom: 2 }}>Market Cap (FDV)</div>
          <div style={{ fontSize: 26, fontWeight: 800, color: C.text, letterSpacing: -0.5 }}>{fmtCap(live?.fdv ?? null)}</div>
        </div>
        <div style={{ paddingBottom: 4 }}>
          <div style={{ fontSize: 11, color: C.muted, marginBottom: 2 }}>Price</div>
          <div style={{ fontSize: 15, fontWeight: 600, color: C.muted }}>{live ? fmtPrice(live.priceUsd) : '…'}</div>
        </div>
        {live && (
          <div style={{ paddingBottom: 4 }}>
            <div style={{ fontSize: 11, color: C.muted, marginBottom: 2 }}>24h</div>
            <div style={{ fontSize: 15, fontWeight: 700, color: chgCol }}>
              {isUp ? '▲' : '▼'} {Math.abs(live.change24h).toFixed(2)}%
            </div>
          </div>
        )}
      </div>

      {/* Timeframe selector */}
      <div style={{ display: 'flex', gap: 4, marginBottom: 10 }}>
        {Object.keys(TF_CONFIG).map(tf => (
          <button
            key={tf}
            style={tf === timeframe ? s.tfBtnActive : s.tfBtn}
            onClick={() => onTimeframe(tf)}
          >{tf}</button>
        ))}
      </div>

      {/* Chart */}
      <div style={{ position: 'relative', minHeight: 180 }}>
        {loadingCandles
          ? <div style={{ height: 180, display: 'flex', alignItems: 'center', justifyContent: 'center', color: C.muted, fontSize: 12 }}>Loading…</div>
          : <CandlestickChart candles={candles} width={chartWidth} height={180} />
        }
      </div>
    </div>
  )
}

// ── Search row ────────────────────────────────────────────────────────────────
function SearchRow({ nodeCode, onAdded }: { nodeCode: string; onAdded: () => void }) {
  const [query,   setQuery]   = useState('')
  const [results, setResults] = useState<SearchResult[]>([])
  const [loading, setLoading] = useState(false)
  const [adding,  setAdding]  = useState<string | null>(null)

  async function search() {
    if (query.trim().length < 2) return
    setLoading(true)
    try {
      const data = await callFunction<{ results: SearchResult[] }>('search-tokens', { query: query.trim() })
      setResults(data?.results ?? [])
    } catch { setResults([]) }
    setLoading(false)
  }

  async function add(r: SearchResult) {
    setAdding(r.pairAddress)
    try {
      await callFunction('add-node-ticker', {
        node_code:    nodeCode,
        pool_address: r.pairAddress,
        chain_id:     r.chainId,
        base_symbol:  r.baseSymbol,
        base_name:    r.baseName,
        quote_symbol: r.quoteSymbol,
      })
      setResults([])
      setQuery('')
      onAdded()
    } catch (e: unknown) {
      alert((e as Error).message ?? 'Could not add ticker')
    }
    setAdding(null)
  }

  return (
    <div style={{ marginBottom: 20 }}>
      <div style={{ display: 'flex', gap: 8 }}>
        <input
          style={s.searchInput}
          value={query}
          onChange={e => setQuery(e.target.value)}
          onKeyDown={e => e.key === 'Enter' && search()}
          placeholder="Search token (name, symbol, or address)…"
        />
        <button style={s.searchBtn} onClick={search} disabled={loading}>
          {loading ? '…' : 'Search'}
        </button>
      </div>

      {results.length > 0 && (
        <div style={s.searchDropdown}>
          {results.map(r => (
            <div key={r.pairAddress} style={s.searchResult}>
              <div style={{ flex: 1, minWidth: 0 }}>
                <span style={{ fontWeight: 700, fontSize: 13, color: C.text }}>{r.baseName}</span>
                <span style={{ fontSize: 11, color: C.muted, marginLeft: 6 }}>{r.baseSymbol}/{r.quoteSymbol}</span>
                <span style={{ fontSize: 11, color: C.muted, marginLeft: 6 }}>· {r.chainId}</span>
                <span style={{ fontSize: 11, color: C.muted, marginLeft: 6 }}>Liq {fmtCap(r.liquidityUsd)}</span>
              </div>
              <button
                style={s.addBtn}
                onClick={() => add(r)}
                disabled={adding === r.pairAddress}
              >{adding === r.pairAddress ? '…' : '+ Add'}</button>
            </div>
          ))}
        </div>
      )}
    </div>
  )
}

// ── Main component ─────────────────────────────────────────────────────────────
export default function TickerBoard({ nodeCode, isOwner }: { nodeCode: string; isOwner: boolean }) {
  const [tickers,      setTickers]      = useState<StoredTicker[]>([])
  const [live,         setLive]         = useState<Record<string, LiveData | null>>({})
  const [sparkCandles, setSparkCandles] = useState<Record<string, Candle[]>>({})
  const [expanded,     setExpanded]     = useState<string | null>(null)
  const [candles,      setCandles]      = useState<Candle[]>([])
  const [loadingC,     setLoadingC]     = useState(false)
  const [timeframe,    setTimeframe]    = useState('1D')
  const [chartWidth,   setChartWidth]   = useState(360)

  // Measure container width for responsive chart
  const containerRef = useCallback((node: HTMLDivElement | null) => {
    if (!node) return
    const obs = new ResizeObserver(entries => {
      const w = entries[0]?.contentRect.width
      if (w) setChartWidth(Math.floor(w) - 32)
    })
    obs.observe(node)
  }, [])

  // Load stored tickers from Supabase
  const loadTickers = useCallback(async () => {
    const { data } = await supabase
      .from('node_ticker_config')
      .select('pool_address, chain_id, base_symbol, base_name, quote_symbol, display_order')
      .eq('node_code', nodeCode)
      .order('display_order')
    setTickers((data ?? []) as StoredTicker[])
  }, [nodeCode])

  useEffect(() => { loadTickers() }, [loadTickers])

  // Fetch live data + 1D sparklines for all tickers
  useEffect(() => {
    if (tickers.length === 0) return
    tickers.forEach(async t => {
      const ld = await fetchLiveData(t.chain_id, t.pool_address)
      setLive(prev => ({ ...prev, [t.pool_address]: ld }))
      // Also fetch 1D candles for sparkline
      const sc = await fetchOHLCV(t.chain_id, t.pool_address, '1D')
      setSparkCandles(prev => ({ ...prev, [t.pool_address]: sc }))
    })
  }, [tickers])

  // Fetch OHLCV when expanded ticker or timeframe changes
  useEffect(() => {
    if (!expanded) return
    const t = tickers.find(t => t.pool_address === expanded)
    if (!t) return
    setLoadingC(true)
    fetchOHLCV(t.chain_id, t.pool_address, timeframe).then(cs => {
      setCandles(cs)
      setLoadingC(false)
    })
  }, [expanded, timeframe, tickers])

  async function removeTicker(poolAddress: string) {
    const t = tickers.find(t => t.pool_address === poolAddress)
    if (!t) return
    try {
      await callFunction('remove-node-ticker', { node_code: nodeCode, pool_address: poolAddress })
      if (expanded === poolAddress) setExpanded(null)
      loadTickers()
    } catch (e: unknown) {
      alert((e as Error).message ?? 'Could not remove ticker')
    }
  }

  if (tickers.length === 0 && !isOwner) return null

  return (
    <div ref={containerRef} style={s.root}>
      <div style={{ display: 'flex', alignItems: 'center', gap: 10, marginBottom: 16 }}>
        <h2 style={s.sectionTitle}>Token Screener</h2>
        <span style={s.count}>{tickers.length}/10</span>
      </div>

      {/* Search bar — owner only, when under 10 tickers */}
      {isOwner && tickers.length < 10 && (
        <SearchRow nodeCode={nodeCode} onAdded={loadTickers} />
      )}

      {tickers.length === 0 && (
        <p style={{ color: C.muted, fontSize: 13 }}>
          {isOwner ? 'Search for a token above to add it to your screener.' : 'No tickers added yet.'}
        </p>
      )}

      {/* Ticker list */}
      {tickers.map(t => {
        const liveD = live[t.pool_address] ?? null
        const sparks = sparkCandles[t.pool_address] ?? []
        if (expanded === t.pool_address) {
          return (
            <ExpandedCard
              key={t.pool_address}
              ticker={t}
              live={liveD}
              candles={candles}
              loadingCandles={loadingC}
              timeframe={timeframe}
              onTimeframe={tf => setTimeframe(tf)}
              onCollapse={() => setExpanded(null)}
              onRemove={() => removeTicker(t.pool_address)}
              isOwner={isOwner}
              chartWidth={chartWidth}
            />
          )
        }
        return (
          <CompactCard
            key={t.pool_address}
            ticker={t}
            live={liveD}
            sparkCandles={sparks}
            onExpand={() => { setExpanded(t.pool_address); setTimeframe('1D') }}
            onRemove={() => removeTicker(t.pool_address)}
            isOwner={isOwner}
          />
        )
      })}
    </div>
  )
}

// ── Styles ─────────────────────────────────────────────────────────────────────
const s: Record<string, React.CSSProperties> = {
  root: { marginBottom: 40 },

  sectionTitle: {
    fontSize: 10, fontWeight: 'bold', color: C.muted,
    textTransform: 'uppercase', letterSpacing: 1.4, margin: 0,
  },
  count: {
    background: '#141414', border: `1px solid ${C.border}`,
    borderRadius: 20, padding: '1px 8px', fontSize: 11, color: C.muted,
  },

  compactCard: {
    display: 'flex', alignItems: 'center', gap: 12,
    background: C.card, border: `1px solid ${C.border}`, borderRadius: 10,
    padding: '10px 12px', marginBottom: 6,
    transition: 'border-color .15s',
  },
  expandedCard: {
    background: C.card, border: `1px solid ${C.border}`, borderRadius: 10,
    padding: '16px', marginBottom: 6,
  },

  logoWrap:    { flexShrink: 0 },
  logoFallback: {
    width: 36, height: 36, borderRadius: '50%',
    background: C.surface, border: `1px solid ${C.border}`,
    display: 'flex', alignItems: 'center', justifyContent: 'center',
    fontSize: 13, fontWeight: 700, color: C.muted, flexShrink: 0,
  },

  iconBtn: {
    width: 28, height: 28, borderRadius: 8,
    background: C.surface, border: `1px solid ${C.border}`,
    color: C.text, fontSize: 14, cursor: 'pointer',
    display: 'flex', alignItems: 'center', justifyContent: 'center',
    padding: 0,
  },

  tfBtn: {
    padding: '4px 10px', borderRadius: 6, fontSize: 11, fontWeight: 600,
    background: 'transparent', border: `1px solid ${C.border}`,
    color: C.muted, cursor: 'pointer',
  },
  tfBtnActive: {
    padding: '4px 10px', borderRadius: 6, fontSize: 11, fontWeight: 600,
    background: C.surface, border: `1px solid #3a3a3a`,
    color: C.text, cursor: 'pointer',
  },

  searchInput: {
    flex: 1, padding: '9px 12px', background: C.surface, color: C.text,
    border: `1px solid ${C.border}`, borderRadius: 8, fontSize: 13,
  },
  searchBtn: {
    padding: '9px 18px', background: C.surface, color: C.text,
    border: `1px solid ${C.border}`, borderRadius: 8, fontSize: 13,
    fontWeight: 600, cursor: 'pointer', flexShrink: 0,
  },
  searchDropdown: {
    background: C.card, border: `1px solid ${C.border}`, borderRadius: 8,
    marginTop: 4, overflow: 'hidden',
  },
  searchResult: {
    display: 'flex', alignItems: 'center', gap: 10, padding: '10px 12px',
    borderBottom: `1px solid ${C.border}`,
  },
  addBtn: {
    padding: '5px 12px', background: 'transparent', border: `1px solid ${C.border}`,
    borderRadius: 6, color: C.green, fontSize: 12, fontWeight: 700, cursor: 'pointer', flexShrink: 0,
  },
}

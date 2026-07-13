// supabase/functions/ticker-stats/index.ts
//
// Backend for the on-device "Ticker Stats" screen (formerly TurboUSD Stats).
// Input:  { chain, pool, symbol? }  (a DEX pool, same identity as node_tickers)
// Output: { symbol, name, fields: [{ label, value }] }  — pre-formatted strings so
//         the device just paints them into its 2x2 grid.
//
// Two paths:
//   1. CUSTOM tokens (₸USD now, DRB/others later): a per-pool handler with its own
//      data source (₸USD → treasury.turbousd.com; DRB → its wallet reads, TBD).
//      Add a new token = add ONE entry to CUSTOM_HANDLERS. The device never changes.
//   2. DEFAULT (everything else): basic stats from DexScreener — price, market cap,
//      24h volume, liquidity. No "burned" (that's a per-token, custom concept).
//
// Deploy with: supabase functions deploy ticker-stats

const CORS: Record<string, string> = {
  'Access-Control-Allow-Origin':  '*',
  'Access-Control-Allow-Methods': 'POST, OPTIONS',
  'Access-Control-Allow-Headers': 'Content-Type, Authorization, apikey',
}

interface Field { label: string; value: string }
// circSupply (optional): circulating supply as a raw number. The device uses it
// to label the chart's Y-axis as MARKET CAP (price × circSupply) instead of raw
// price. Only ₸USD/custom tokens that know their supply set it.
// logoUrl (optional): DexScreener info.imageUrl — the device downloads it and
// paints it in the centre of the stat grid.
interface StatsOut { symbol: string; name: string; fields: Field[]; circSupply?: number; logoUrl?: string }

// ── formatting helpers ────────────────────────────────────────────────────────
function fmtCompact(n: number): string {
  if (!isFinite(n) || n === 0) return '0'
  const abs = Math.abs(n)
  if (abs >= 1e12) return (n / 1e12).toFixed(2).replace(/\.?0+$/, '') + 'T'
  if (abs >= 1e9)  return (n / 1e9).toFixed(2).replace(/\.?0+$/, '') + 'B'
  if (abs >= 1e6)  return (n / 1e6).toFixed(2).replace(/\.?0+$/, '') + 'M'
  if (abs >= 1e3)  return (n / 1e3).toFixed(1).replace(/\.?0+$/, '') + 'K'
  return n.toFixed(0)
}
function fmtUsdCompact(n: number): string { return '$' + fmtCompact(n) }
function fmtPrice(p: number): string {
  if (!isFinite(p) || p <= 0) return '$0'
  if (p >= 1)     return '$' + p.toLocaleString('en-US', { maximumFractionDigits: 2 })
  if (p >= 0.01)  return '$' + p.toFixed(4)
  if (p >= 0.0001) return '$' + p.toFixed(6)
  // sub-$0.0001: 4 significant figures in the "0.0…" DexScreener style, but as a
  // plain string the device font can render (e.g. "$0.0₅1234" → "$0.00001234").
  const lead = Math.floor(-Math.log10(p))            // # of zeros after the point
  const mant = Math.round(p * Math.pow(10, lead + 3)) // 4 significant digits
  return '$0.' + '0'.repeat(Math.max(0, lead)) + String(mant).replace(/0+$/, '')
}

// ── DexScreener (default source) ──────────────────────────────────────────────
async function dexPair(chain: string, pool: string): Promise<any | null> {
  try {
    const r = await fetch(`https://api.dexscreener.com/latest/dex/pairs/${chain}/${pool}`)
    if (!r.ok) return null
    const j = await r.json()
    return (j.pairs && j.pairs[0]) || (j.pair ?? null)
  } catch { return null }
}

async function defaultStats(chain: string, pool: string, symbolHint: string): Promise<StatsOut> {
  const p = await dexPair(chain, pool)
  const sym = (p?.baseToken?.symbol || symbolHint || '—').toString()
  const name = (p?.baseToken?.name || sym).toString()
  if (!p) {
    return { symbol: sym, name, fields: [{ label: 'PRICE', value: '—' }, { label: 'MARKET CAP', value: '—' }] }
  }
  const price = Number(p.priceUsd) || 0
  const mcap  = Number(p.marketCap) || Number(p.fdv) || 0
  const vol24 = Number(p.volume?.h24) || 0
  const liq   = Number(p.liquidity?.usd) || 0
  return {
    symbol: sym,
    name,
    logoUrl: p.info?.imageUrl || '',
    fields: [
      { label: 'PRICE',      value: fmtPrice(price) },
      { label: 'MARKET CAP', value: mcap ? fmtUsdCompact(mcap) : '—' },
      { label: '24H VOLUME', value: vol24 ? fmtUsdCompact(vol24) : '—' },
      { label: 'LIQUIDITY',  value: liq ? fmtUsdCompact(liq) : '—' },
    ],
  }
}

// ── CUSTOM: ₸USD (treasury.turbousd.com) ──────────────────────────────────────
const TUSD_POOL  = '0xd013725b904e76394A3aB0334Da306C505D778F8'
async function tusdStats(): Promise<StatsOut> {
  try {
    const r = await fetch('https://treasury.turbousd.com/api/treasury-data')
    const j = await r.json()
    const supply = Number(j.tusdSupplyNum) || 0
    const burned = Number(j.tusdBurnedNum) || 0
    const price  = Number(j.tusdPriceUsd) || 0
    const circ   = Math.max(0, supply - burned)
    // ₸USD intentionally has NO logo (per design) — the grid stays clean.
    return {
      symbol: 'TUSD',
      name: 'TurboUSD',
      circSupply: circ,
      fields: [
        { label: 'SUPPLY',      value: fmtCompact(supply) },
        { label: 'PRICE',       value: fmtPrice(price) },
        { label: 'TOTAL BURNED', value: fmtCompact(burned) },
        { label: 'MARKET CAP',  value: fmtUsdCompact(price * circ) },
      ],
    }
  } catch {
    return { symbol: 'TUSD', name: 'TurboUSD', fields: [{ label: 'PRICE', value: '—' }] }
  }
}

// ── CUSTOM: DRB (wallet-composition view) ─────────────────────────────────────
// DRB's screen doesn't show price — it shows what the DRB wallet HOLDS: WETH,
// DRB and USDC, plus the token's circulating market cap. WETH and DRB lines show
// the holding AND its USD equivalent (second line); USDC is a dollar stable so
// its USD equivalent is redundant and omitted (shown as a plain amount).
//
// TODO(david): fill these in from the DRB source you'll point me to (the bot in
// github.com/TurboUSD/DRB has the read logic). Needed:
//   DRB_WALLET   — the wallet whose balances we display
//   DRB_TOKEN    — DRB ERC-20 address (for balanceOf + decimals)
//   DRB_POOL     — DRB's DEX pool (registry key below + market cap source)
// Reads: balanceOf(WETH), balanceOf(DRB), balanceOf(USDC) on the wallet via an
// RPC; ETH price + DRB price from DexScreener. Two-line values use "\n" — the
// device renders the second line under the first.
const DRB_WALLET = '' // TODO
const DRB_TOKEN  = '' // TODO
const DRB_POOL   = '' // TODO
async function drbStats(): Promise<StatsOut> {
  // Until the addresses are wired, fall back to a basic DexScreener read so the
  // screen still shows something rather than erroring.
  if (!DRB_WALLET || !DRB_POOL) return defaultStats('base', DRB_POOL || TUSD_POOL, 'DRB')

  // --- Scaffold of the intended output (enable once reads are wired) ---
  // const wethAmt = await erc20Balance(WETH, DRB_WALLET)  // 18 decimals
  // const drbAmt  = await erc20Balance(DRB_TOKEN, DRB_WALLET)
  // const usdcAmt = await erc20Balance(USDC, DRB_WALLET)  // 6 decimals
  // const ethUsd  = await priceUsd(WETH_POOL)
  // const drbUsd  = await priceUsd(DRB_POOL)
  // const mcap    = await marketCap(DRB_POOL)
  // return {
  //   symbol: 'DRB', name: 'DRB', logoUrl: await poolLogo(DRB_POOL),
  //   fields: [
  //     { label: 'WETH HELD', value: `${fmtAmt(wethAmt)}\n≈ ${fmtUsdCompact(wethAmt * ethUsd)}` },
  //     { label: 'DRB HELD',  value: `${fmtCompact(drbAmt)}\n≈ ${fmtUsdCompact(drbAmt * drbUsd)}` },
  //     { label: 'USDC HELD', value: fmtCompact(usdcAmt) },                       // no USD line
  //     { label: 'MARKET CAP', value: fmtUsdCompact(mcap) },
  //   ],
  // }
  return defaultStats('base', DRB_POOL, 'DRB')
}

// Per-pool custom handlers (lowercase pool address → handler). Add tokens here.
const CUSTOM_HANDLERS: Record<string, () => Promise<StatsOut>> = {
  '0xd013725b904e76394a3ab0334da306c505d778f8': tusdStats,   // ₸USD
  // [DRB_POOL.toLowerCase()]: drbStats,   // enable once DRB addresses are wired
}
// Referenced so TS/lint don't flag the scaffold as dead while addresses are TODO.
void drbStats

Deno.serve(async (req: Request) => {
  if (req.method === 'OPTIONS') return new Response(null, { headers: CORS })
  const send = (body: unknown, status = 200) =>
    new Response(JSON.stringify(body), { status, headers: { ...CORS, 'Content-Type': 'application/json' } })

  try {
    let chain = '', pool = '', symbol = ''
    if (req.method === 'POST') {
      const b = await req.json().catch(() => ({}))
      chain = (b.chain || '').toString(); pool = (b.pool || '').toString(); symbol = (b.symbol || '').toString()
    } else {
      const u = new URL(req.url)
      chain = u.searchParams.get('chain') || ''; pool = u.searchParams.get('pool') || ''; symbol = u.searchParams.get('symbol') || ''
    }
    if (!pool) return send({ error: 'pool is required' }, 400)

    const handler = CUSTOM_HANDLERS[pool.toLowerCase()]
    const out = handler ? await handler() : await defaultStats(chain || 'base', pool, symbol)
    return send(out)
  } catch (err) {
    return send({ error: `Internal error: ${(err as Error).message}` }, 500)
  }
})

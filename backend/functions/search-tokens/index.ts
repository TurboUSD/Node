// supabase/functions/search-tokens/index.ts
//
// Proxies DexScreener search so the web UI can find tokens by ticker or name.
// Returns top 12 results sorted by liquidity (descending).
//
// Called from: setup page ticker search UI
// Auth: public (anon key), rate-limited by Supabase
//
// Deploy: supabase functions deploy search-tokens

const DEXSCREENER_URL = 'https://api.dexscreener.com/latest/dex/search'

const CORS = {
  'Access-Control-Allow-Origin':  '*',
  'Access-Control-Allow-Methods': 'POST, OPTIONS',
  'Access-Control-Allow-Headers': 'Content-Type, Authorization, apikey',
  'Content-Type': 'application/json',
}

interface DexPair {
  chainId:    string
  pairAddress: string
  baseToken:  { address: string; symbol: string; name: string }
  quoteToken: { symbol: string }
  priceUsd?:  string
  liquidity?: { usd?: number }
  volume?:    { h24?: number }
  priceChange?: { h24?: number }
  fdv?:       number
}

Deno.serve(async (req: Request) => {
  if (req.method === 'OPTIONS') {
    return new Response(null, { headers: CORS })
  }

  let query = ''
  try {
    const body = await req.json()
    query = (body.query ?? '').trim()
  } catch {
    return new Response(JSON.stringify({ error: 'Invalid JSON body' }), { status: 400, headers: CORS })
  }

  if (!query || query.length < 1) {
    return new Response(JSON.stringify({ pairs: [] }), { headers: CORS })
  }

  try {
    const res = await fetch(`${DEXSCREENER_URL}/?q=${encodeURIComponent(query)}`, {
      headers: { 'User-Agent': 'TurboUSD-Node/1.0' },
    })

    if (!res.ok) {
      return new Response(JSON.stringify({ error: 'DexScreener unavailable' }), { status: 502, headers: CORS })
    }

    const data = await res.json()
    const pairs: DexPair[] = data.pairs ?? []

    // Sort by liquidity descending, take top 12
    const top = pairs
      .filter(p => p.priceUsd && (p.liquidity?.usd ?? 0) > 1000)
      .sort((a, b) => (b.liquidity?.usd ?? 0) - (a.liquidity?.usd ?? 0))
      .slice(0, 12)
      .map(p => ({
        pairAddress:   p.pairAddress,
        chainId:       p.chainId,
        baseSymbol:    p.baseToken.symbol,
        baseName:      p.baseToken.name,
        baseAddress:   p.baseToken.address,
        quoteSymbol:   p.quoteToken.symbol,
        priceUsd:      p.priceUsd ? parseFloat(p.priceUsd) : null,
        liquidityUsd:  p.liquidity?.usd ?? null,
        volume24h:     p.volume?.h24 ?? null,
        priceChange24h: p.priceChange?.h24 ?? null,
        fdv:           p.fdv ?? null,
      }))

    return new Response(JSON.stringify({ pairs: top }), { headers: CORS })
  } catch (err) {
    return new Response(
      JSON.stringify({ error: 'Search failed', detail: String(err) }),
      { status: 500, headers: CORS }
    )
  }
})

// supabase/functions/resolve-nft/index.ts
//
// Resolves metadata for individual NFTs from OpenSea API v2.
// Used by the web setup page when a user pastes an OpenSea URL in Manual Picks mode.
//
// Input:  { items: string[] }   // each: "chain:contract:tokenId" — max 20
// Output: { results: NftResult[] }
//
// Required DB column for storing the pinlist:
//   ALTER TABLE nodes ADD COLUMN IF NOT EXISTS nft_pinlist text;
//
// Deploy with: supabase functions deploy resolve-nft

const OPENSEA_BASE = 'https://api.opensea.io/api/v2'
const MAX_ITEMS    = 20

interface NftResult {
  id:               string    // original "chain:contract:tokenId" key
  chain:            string
  contract:         string
  tokenId:          string
  name?:            string
  image_url?:       string
  collection_name?: string
  floor_price_eth?: number
  error?:           string
}

Deno.serve(async (req: Request) => {
  if (req.method === 'OPTIONS') {
    return new Response(null, {
      headers: {
        'Access-Control-Allow-Origin':  '*',
        'Access-Control-Allow-Methods': 'POST, OPTIONS',
        'Access-Control-Allow-Headers': 'Content-Type, Authorization, apikey',
      },
    })
  }

  if (req.method !== 'POST') {
    return new Response(JSON.stringify({ error: 'Method not allowed' }), { status: 405 })
  }

  let body: { items?: unknown }
  try {
    body = await req.json()
  } catch {
    return new Response(JSON.stringify({ error: 'Invalid JSON' }), { status: 400 })
  }

  if (!Array.isArray(body.items) || body.items.length === 0) {
    return new Response(JSON.stringify({ error: 'items must be a non-empty array' }), { status: 400 })
  }

  const apiKey = Deno.env.get('OPENSEA_API_KEY') ?? ''
  const osHeaders: Record<string, string> = { 'Accept': 'application/json' }
  if (apiKey) osHeaders['X-API-KEY'] = apiKey

  const items = (body.items as unknown[])
    .filter(i => typeof i === 'string')
    .slice(0, MAX_ITEMS) as string[]

  const results: NftResult[] = []

  for (const item of items) {
    // Expect format: "chain:contract:tokenId"
    // contract may start with 0x and tokenId is numeric — split on first two colons
    const firstColon  = item.indexOf(':')
    const secondColon = item.indexOf(':', firstColon + 1)

    if (firstColon < 1 || secondColon < 1) {
      results.push({ id: item, chain: '', contract: '', tokenId: '', error: 'Invalid format — expected chain:contract:tokenId' })
      continue
    }

    const chain    = item.slice(0, firstColon).toLowerCase()
    const contract = item.slice(firstColon + 1, secondColon).toLowerCase()
    const tokenId  = item.slice(secondColon + 1)

    if (!chain || !contract || !tokenId) {
      results.push({ id: item, chain, contract, tokenId, error: 'Missing chain, contract, or tokenId' })
      continue
    }

    try {
      // 1. Fetch NFT metadata
      const nftUrl = `${OPENSEA_BASE}/chain/${chain}/contract/${contract}/nfts/${tokenId}`
      const nftRes = await fetch(nftUrl, { headers: osHeaders })

      if (!nftRes.ok) {
        results.push({ id: item, chain, contract, tokenId, error: `OpenSea returned ${nftRes.status} for NFT lookup` })
        continue
      }

      const nftData: { nft?: Record<string, unknown> } = await nftRes.json()
      const nft = nftData.nft ?? {}

      const name      = (nft.name as string | undefined)      ?? `#${tokenId}`
      const image_url = (nft.display_image_url as string | undefined)
                     ?? (nft.image_url as string | undefined)
                     ?? (nft.metadata_url as string | undefined)
                     ?? ''
      const slug      = (nft.collection as string | undefined) ?? ''

      // 2. Fetch collection stats for floor price + display name (optional — degrade gracefully)
      let floor_price_eth = 0
      let collection_name = slug

      if (slug) {
        try {
          const statsRes = await fetch(`${OPENSEA_BASE}/collections/${slug}/stats`, { headers: osHeaders })
          if (statsRes.ok) {
            const statsData: { total?: { floor_price?: number }; name?: string } = await statsRes.json()
            floor_price_eth = statsData.total?.floor_price ?? 0
            if (statsData.name) collection_name = statsData.name
          }
        } catch {
          // non-fatal — floor price and collection name are optional
        }
      }

      results.push({ id: item, chain, contract, tokenId, name, image_url, collection_name, floor_price_eth })

    } catch (e) {
      results.push({ id: item, chain, contract, tokenId, error: String(e) })
    }
  }

  return new Response(JSON.stringify({ results }), {
    status: 200,
    headers: {
      'Content-Type': 'application/json',
      'Access-Control-Allow-Origin': '*',
    },
  })
})

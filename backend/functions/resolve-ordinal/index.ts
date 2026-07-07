// resolve-ordinal — metadata for Bitcoin Ordinals inscriptions. NO API KEYS.
//
// POST { ids: string[] }  (inscription ids, max 20)
// →    { results: [{ id, name, collection, floor_btc, bg } | { id, error }] }
//
// Source chain — every endpoint below was verified LIVE on 2026-07-06 with a
// real NodeMonke inscription (8aaead6a…7ci0):
//
//   1. Ordinals Wallet  GET turbo.ordinalswallet.com/inscription/:id
//      → { num, meta:{ name:"nodemonke 7548", attributes:[…] },
//          collection:{ name:"NodeMonkes", slug:"nodemonkes" } }
//      NOTE the host: turbo.ordinalswallet.com. The old api.ordinalswallet.com
//      host silently returns nothing — that's why earlier versions failed.
//   2. Ordinals Wallet  GET turbo.ordinalswallet.com/collection/:slug/stats
//      → { floor_price: 3897630 }   (SATS)
//   3. ordinals.com     GET /r/inscription/:id   (ord recursive endpoint,
//      plain JSON, no Accept header games) → { number: 108189 } as the last
//      resort for at least "Ordinal #108189".
//
// Dead sources, deliberately REMOVED (do not re-add):
//   • Magic Eden — Bitcoin marketplace closed 2026-03-09, its BTC API shut
//     down 2026-03-27.
//   • Hiro Ordinals API — deprecated 2026-03-09.
//   • Satflow — requires an API key.
//
// Deploy: supabase functions deploy resolve-ordinal --project-ref jaiqnucohafuxwnjxdoo

import { serve } from 'https://deno.land/std@0.168.0/http/server.ts'

const CORS = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Headers': 'authorization, x-client-info, apikey, content-type',
}

const OW = 'https://turbo.ordinalswallet.com'

const HDRS = {
  accept: 'application/json',
  'user-agent': 'TurboUSD-Node/1.0 (+https://network.turbousd.com)',
}

// Background-trait value → "#RRGGBB" (hex passthrough + common colour names).
function colourToHex(raw: string): string | null {
  const v = raw.trim().toLowerCase()
  if (/^#[0-9a-f]{6}$/.test(v)) return v
  const NAMED: Record<string, string> = {
    orange: '#f68b1f', blue: '#4a7cf0', green: '#3fa34d', red: '#e5484d',
    purple: '#8e4ec6', yellow: '#f5d90a', pink: '#e93d82', brown: '#8a6a4b',
    gray: '#8d8d8d', grey: '#8d8d8d', black: '#000000', white: '#ffffff',
    cyan: '#0fc0d8', magenta: '#d6409f', teal: '#12a594', lime: '#99d52a',
  }
  return NAMED[v] ?? null
}

// deno-lint-ignore no-explicit-any
function bgFromAttrs(attrs: any): string | null {
  if (!Array.isArray(attrs)) return null
  const a = attrs.find((x: { trait_type?: string; value?: string }) =>
    /^background/i.test(String(x?.trait_type ?? '')))
  return a ? colourToHex(String(a.value ?? '')) : null
}

// "nodemonke 7548" + collection "NodeMonkes" → "NodeMonke #7548".
// Indexer names are often lowercase with a bare number; the collection name
// carries the proper casing, so borrow it when the base word matches.
function prettyName(raw: string | null, coll: string | null): string | null {
  if (!raw) return null
  const m = raw.match(/^(.*?)[\s#]+(\d+)$/)
  if (!m) return raw
  const base = m[1].trim()
  if (base && coll && coll.toLowerCase().startsWith(base.toLowerCase())) {
    return `${coll.slice(0, base.length)} #${m[2]}`
  }
  return base ? `${base} #${m[2]}` : raw
}

async function getJson(url: string, tag: string, id: string) {
  try {
    const res = await fetch(url, { headers: HDRS, signal: AbortSignal.timeout(6000) })
    console.log(`resolve-ordinal ${id.slice(0, 8)}: ${tag} ${res.status}`)
    if (!res.ok) return null
    return await res.json()
  } catch (e) {
    console.log(`resolve-ordinal ${id.slice(0, 8)}: ${tag} threw ${e}`)
    return null
  }
}

serve(async (req) => {
  if (req.method === 'OPTIONS') return new Response('ok', { headers: CORS })
  try {
    const { ids } = await req.json()
    if (!Array.isArray(ids) || ids.length === 0 || ids.length > 20)
      return new Response(JSON.stringify({ error: 'ids: 1-20 inscription ids' }),
        { status: 400, headers: { ...CORS, 'Content-Type': 'application/json' } })

    // Collection floors are fetched once per slug per request, not per item.
    const floorCache = new Map<string, number | null>()

    const results = []
    for (const id of ids) {
      if (!/^[0-9a-f]{64}i[0-9]+$/i.test(String(id))) {
        results.push({ id, error: 'invalid inscription id' })
        continue
      }
      let name: string | null = null
      let collName: string | null = null
      let floorBtc: number | null = null
      let bg: string | null = null
      let num: number | null = null

      // ── 1+2. Ordinals Wallet: item meta + collection floor ────────────────
      const ins = await getJson(`${OW}/inscription/${id}`, 'ow-inscription', id)
      if (ins) {
        num      = typeof ins.num === 'number' ? ins.num : null
        collName = ins?.collection?.name ?? null
        name     = prettyName(ins?.meta?.name ?? null, collName)
        bg       = bgFromAttrs(ins?.meta?.attributes)

        const slug = ins?.collection?.slug
        if (slug) {
          if (!floorCache.has(slug)) {
            const st = await getJson(`${OW}/collection/${slug}/stats`, 'ow-stats', id)
            const sats = Number(st?.floor_price ?? 0)
            floorCache.set(slug, sats > 0 ? sats / 1e8 : null)
          }
          floorBtc = floorCache.get(slug) ?? null
        }
        // Item response sometimes carries the floor directly — use as backup.
        if (floorBtc === null) {
          const sats = Number(ins?.collection?.floor_price ?? 0)
          if (sats > 0) floorBtc = sats / 1e8
        }
        if (!name && num !== null) name = `Ordinal #${num}`
      }

      // ── 3. ordinals.com recursive endpoint: inscription number ────────────
      if (!name) {
        const o = await getJson(`https://ordinals.com/r/inscription/${id}`, 'ordinals.com', id)
        if (o?.number !== undefined && o?.number !== null) name = `Ordinal #${o.number}`
      }

      console.log(`resolve-ordinal ${id.slice(0, 8)}: → name=${name} coll=${collName} floor=${floorBtc} bg=${bg}`)
      results.push({ id, name, collection: collName, floor_btc: floorBtc, bg })
    }
    return new Response(JSON.stringify({ results }),
      { status: 200, headers: { ...CORS, 'Content-Type': 'application/json' } })
  } catch (e) {
    return new Response(JSON.stringify({ error: e instanceof Error ? e.message : 'bad request' }),
      { status: 500, headers: { ...CORS, 'Content-Type': 'application/json' } })
  }
})

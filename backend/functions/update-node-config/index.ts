// supabase/functions/update-node-config/index.ts
//
// Called from the web setup page (turbousd.com/setup/{nodeCode}) when the
// owner changes their profile or display preferences. Devices pick these up
// on their next heartbeat/poll cycle.
//
// Accepted fields:
//   Profile:     display_name, bio, wallet_address, twitter_handle, country, city
//   Preferences: temp_unit, date_format, time_format, alarm_hour, alarm_minute, alarm_enabled
//   NFT Gallery: nft_wallet_address, nft_grid_size (1|4|9), nft_carousel_enabled, nft_slideshow_secs
//
// Required DB columns for NFT gallery (add via migration before deploying):
//   ALTER TABLE nodes ADD COLUMN IF NOT EXISTS nft_wallet_address text;
//   ALTER TABLE nodes ADD COLUMN IF NOT EXISTS nft_grid_size smallint DEFAULT 9;
//   ALTER TABLE nodes ADD COLUMN IF NOT EXISTS nft_carousel_enabled boolean DEFAULT true;
//   ALTER TABLE nodes ADD COLUMN IF NOT EXISTS nft_slideshow_secs smallint DEFAULT 10;
//
// Required DB column for screen order:
//   ALTER TABLE nodes ADD COLUMN IF NOT EXISTS screen_order text;
//
// Required DB column for NFT manual pinlist (max 20 items, "chain:contract:tokenId" CSV):
//   ALTER TABLE nodes ADD COLUMN IF NOT EXISTS nft_pinlist text;
//
// Required DB column for alarm volume (1–5, default 2):
//   ALTER TABLE nodes ADD COLUMN IF NOT EXISTS alarm_volume smallint DEFAULT 2;
//
// Required DB column for screen brightness (1–5, default 5 = full brightness):
//   ALTER TABLE nodes ADD COLUMN IF NOT EXISTS screen_brightness smallint DEFAULT 5;
//
// Required DB columns for screen timeout:
//   ALTER TABLE nodes ADD COLUMN IF NOT EXISTS screen_always_on boolean DEFAULT true;
//   ALTER TABLE nodes ADD COLUMN IF NOT EXISTS screen_timeout_mins smallint DEFAULT 10;
//
// Deploy with: supabase functions deploy update-node-config

import { createClient } from 'https://esm.sh/@supabase/supabase-js@2'

const supabaseUrl = Deno.env.get('SUPABASE_URL')!
const serviceRoleKey = Deno.env.get('SUPABASE_SERVICE_ROLE_KEY')!

const MAX_NAME_LENGTH = 24
const MAX_BIO_LENGTH  = 160
const MAX_CITY_LENGTH = 64
const EVM_ADDRESS_RE  = /^0x[0-9a-fA-F]{40}$/
const TWITTER_RE      = /^[A-Za-z0-9_]{1,50}$/ // without the @

const CORS_HEADERS: Record<string, string> = {
  'Access-Control-Allow-Origin':  '*',
  'Access-Control-Allow-Methods': 'POST, OPTIONS',
  'Access-Control-Allow-Headers': 'Content-Type, Authorization, apikey',
}

// CORS wrapper: the browser preflights JSON POSTs with an OPTIONS request;
// without these headers every call from network.turbousd.com died in the
// browser as a generic "Failed to fetch" (the function itself was fine).
Deno.serve(async (req: Request) => {
  if (req.method === 'OPTIONS') return new Response(null, { headers: CORS_HEADERS })
  let res: Response
  try {
    res = await handle(req)
  } catch (err) {
    res = new Response(JSON.stringify({ error: `Internal error: ${(err as Error).message}` }), { status: 500 })
  }
  const headers = new Headers(res.headers)
  for (const [k, v] of Object.entries(CORS_HEADERS)) headers.set(k, v)
  if (!headers.has('Content-Type')) headers.set('Content-Type', 'application/json')
  return new Response(res.body, { status: res.status, headers })
})

async function handle(req: Request): Promise<Response> {
  if (req.method !== 'POST') {
    return new Response(JSON.stringify({ error: 'Method not allowed' }), { status: 405 })
  }

  let body: {
    node_code?:              string
    setup_token?:            string  // REQUIRED — per-device owner secret from the Settings QR
    display_name?:           string
    bio?:                    string
    wallet_address?:         string
    twitter_handle?:         string
    country?:                string
    city?:                   string
    temp_unit?:              'C' | 'F'
    date_format?:            'DD/MM' | 'MM/DD'
    time_format?:            '24H' | 'AMPM'
    alarm_hour?:             number
    alarm_minute?:           number
    alarm_enabled?:          boolean
    alarm_volume?:           number  // 1–5
    screen_brightness?:      number   // 1–5, default 5
    screen_always_on?:       boolean  // default true
    screen_timeout_mins?:    number   // 1 | 5 | 10 | 30, default 10
    // Auto screen carousel: cycle through every screen on a timer. Default off.
    screen_carousel?:        boolean
    screen_carousel_secs?:   number   // seconds per screen, default 10 (5–120)
    // NFT Gallery settings
    nft_wallet_address?:     string
    nft_grid_size?:          1 | 4 | 9
    nft_carousel_enabled?:   boolean
    nft_show_data?:          boolean
    ticker_cols?:            1 | 2
    nft_coll_order?:         string
    nft_coll_hidden?:        string
    screen_hidden?:          string
    nft_slideshow_secs?:     number
    // Screen swipe order: comma-separated ScreenId integers, e.g. "0,1,2,3,4,5,6".
    // Must be exactly 7 values (0-6), position 0 must be 0 (Home/Clock).
    screen_order?:           string
    // NFT manual pinlist: comma-separated "chain:contract:tokenId" entries, max 20.
    // Takes priority over nft_wallet_address on the device. Pass null/'' to clear.
    nft_pinlist?:            string | null
  }

  try {
    body = await req.json()
  } catch {
    return new Response(JSON.stringify({ error: 'Invalid JSON body' }), { status: 400 })
  }

  if (!body.node_code) {
    return new Response(JSON.stringify({ error: 'node_code is required' }), { status: 400 })
  }

  // --- Validation ---
  if (body.display_name && body.display_name.length > MAX_NAME_LENGTH)
    return new Response(JSON.stringify({ error: `display_name must be ${MAX_NAME_LENGTH} chars or fewer` }), { status: 400 })

  if (body.bio && body.bio.length > MAX_BIO_LENGTH)
    return new Response(JSON.stringify({ error: `bio must be ${MAX_BIO_LENGTH} chars or fewer` }), { status: 400 })

  if (body.wallet_address && body.wallet_address.trim() !== '' && !EVM_ADDRESS_RE.test(body.wallet_address.trim()))
    return new Response(JSON.stringify({ error: 'wallet_address must be a valid 0x EVM address' }), { status: 400 })

  if (body.twitter_handle) {
    const handle = body.twitter_handle.replace(/^@/, '').trim()
    if (!TWITTER_RE.test(handle))
      return new Response(JSON.stringify({ error: 'twitter_handle must be 1-50 alphanumeric/underscore characters (no @)' }), { status: 400 })
    body.twitter_handle = handle
  }

  if (body.city && body.city.length > MAX_CITY_LENGTH)
    return new Response(JSON.stringify({ error: `city must be ${MAX_CITY_LENGTH} chars or fewer` }), { status: 400 })

  // --- Build update payload ---
  const updates: Record<string, unknown> = {}
  if (body.display_name  !== undefined) updates.display_name  = body.display_name?.trim() ?? null
  if (body.bio           !== undefined) updates.bio           = body.bio?.trim() ?? null
  if (body.wallet_address !== undefined) updates.wallet_address = body.wallet_address?.trim() ?? null
  if (body.twitter_handle !== undefined) updates.twitter_handle = body.twitter_handle
  if (body.country       !== undefined) updates.country       = body.country
  if (body.city          !== undefined) updates.city          = body.city?.trim() ?? null
  if (body.temp_unit     !== undefined) updates.temp_unit     = body.temp_unit
  if (body.date_format   !== undefined) updates.date_format   = body.date_format
  if (body.time_format   !== undefined) updates.time_format   = body.time_format
  if (body.alarm_hour    !== undefined) updates.alarm_hour    = body.alarm_hour
  if (body.alarm_minute  !== undefined) updates.alarm_minute  = body.alarm_minute
  if (body.alarm_enabled !== undefined) updates.alarm_enabled = body.alarm_enabled
  if (body.alarm_volume  !== undefined) {
    const v = Math.round(body.alarm_volume)
    if (v < 1 || v > 5)
      return new Response(JSON.stringify({ error: 'alarm_volume must be 1–5' }), { status: 400 })
    updates.alarm_volume = v
  }
  if (body.screen_brightness !== undefined) {
    const v = Math.round(body.screen_brightness)
    if (v < 1 || v > 5)
      return new Response(JSON.stringify({ error: 'screen_brightness must be 1–5' }), { status: 400 })
    updates.screen_brightness = v
  }
  if (body.screen_always_on !== undefined) {
    updates.screen_always_on = body.screen_always_on
  }
  if (body.screen_timeout_mins !== undefined) {
    if (![1, 5, 10, 30].includes(body.screen_timeout_mins))
      return new Response(JSON.stringify({ error: 'screen_timeout_mins must be 1, 5, 10, or 30' }), { status: 400 })
    updates.screen_timeout_mins = body.screen_timeout_mins
  }
  if (body.screen_carousel !== undefined) {
    updates.screen_carousel = !!body.screen_carousel
  }
  if (body.screen_carousel_secs !== undefined) {
    const s = Math.round(Number(body.screen_carousel_secs))
    if (!Number.isFinite(s) || s < 5 || s > 120)
      return new Response(JSON.stringify({ error: 'screen_carousel_secs must be 5-120' }), { status: 400 })
    updates.screen_carousel_secs = s
  }

  // NFT Gallery fields
  if (body.nft_wallet_address !== undefined) {
    const nw = body.nft_wallet_address?.trim() ?? ''
    if (nw !== '' && !EVM_ADDRESS_RE.test(nw))
      return new Response(JSON.stringify({ error: 'nft_wallet_address must be a valid 0x EVM address' }), { status: 400 })
    updates.nft_wallet_address = nw || null
  }
  if (body.ticker_cols !== undefined) {
    if (![1, 2].includes(body.ticker_cols))
      return new Response(JSON.stringify({ error: 'ticker_cols must be 1 or 2' }), { status: 400 })
    updates.ticker_cols = body.ticker_cols
  }
  if (body.nft_show_data   !== undefined) updates.nft_show_data   = !!body.nft_show_data
  if (body.nft_coll_order  !== undefined) updates.nft_coll_order  = (body.nft_coll_order  ?? '').slice(0, 500)
  if (body.nft_coll_hidden !== undefined) updates.nft_coll_hidden = (body.nft_coll_hidden ?? '').slice(0, 500)
  if (body.screen_hidden   !== undefined) updates.screen_hidden   = (body.screen_hidden   ?? '').slice(0, 40)
  if (body.nft_grid_size !== undefined) {
    if (![1, 4, 9].includes(body.nft_grid_size))
      return new Response(JSON.stringify({ error: 'nft_grid_size must be 1, 4, or 9' }), { status: 400 })
    updates.nft_grid_size = body.nft_grid_size
  }
  if (body.nft_carousel_enabled !== undefined) updates.nft_carousel_enabled = body.nft_carousel_enabled
  if (body.nft_slideshow_secs !== undefined) {
    if (body.nft_slideshow_secs < 0 || body.nft_slideshow_secs > 120)
      return new Response(JSON.stringify({ error: 'nft_slideshow_secs must be 0–120' }), { status: 400 })
    updates.nft_slideshow_secs = body.nft_slideshow_secs
  }
  if (body.screen_order !== undefined && body.screen_order !== null) {
    // Validate: exactly 7 comma-separated integers 0-6, all unique, position 0 must be 0
    const parts = body.screen_order.split(',').map(s => parseInt(s.trim(), 10))
    const valid =
      parts.length === 7 &&
      parts.every(n => n >= 0 && n <= 6) &&
      new Set(parts).size === 7 &&
      parts[0] === 0
    if (!valid)
      return new Response(JSON.stringify({ error: 'screen_order must be 7 unique integers 0-6 with 0 first, e.g. "0,1,2,3,4,5,6"' }), { status: 400 })
    updates.screen_order = body.screen_order
  }
  if (body.nft_pinlist !== undefined) {
    // null or empty string → clear pinlist (device falls back to wallet mode)
    if (!body.nft_pinlist) {
      updates.nft_pinlist = null
    } else {
      const pinItems = body.nft_pinlist.split(',').map(s => s.trim()).filter(Boolean)
      if (pinItems.length > 20)
        return new Response(JSON.stringify({ error: 'nft_pinlist max 20 items' }), { status: 400 })
      // EVM item:     chain:0xcontract:tokenId (contract = 0x + 40 hex)
      // Ordinals item: ord:<inscriptionId>:0 with an OPTIONAL 4th field — a
      // user-picked background colour ("ord:<id>:0:#f68b1f"). On-chain art is
      // often transparent and no keyless indexer serves the Background trait,
      // so the owner chooses it in the web editor.
      const validEvm = /^[a-z]+:0x[0-9a-f]{40}:[0-9]+$/i
      const validOrd = /^ord:[0-9a-f]{64}i[0-9]+:[0-9]+(:#[0-9a-f]{6})?$/i
      for (const item of pinItems) {
        if (!validEvm.test(item) && !validOrd.test(item))
          return new Response(JSON.stringify({ error: `Invalid nft_pinlist item: "${item}". Expected chain:0xcontract:tokenId or ord:<inscriptionId>:0` }), { status: 400 })
      }
      updates.nft_pinlist = pinItems.join(',')
    }
  }

  if (Object.keys(updates).length === 0) {
    return new Response(JSON.stringify({ error: 'No fields to update' }), { status: 400 })
  }

  const supabase = createClient(supabaseUrl, serviceRoleKey)

  // ── Owner check ─────────────────────────────────────────────────────────
  // Config changes require the per-device setup token (embedded in the QR on
  // the device's Settings popup). Knowing the public 4-char node code is NOT
  // enough to edit someone else's node.
  const { data: tokenNode, error: tokenLookupErr } = await supabase
    .from('nodes')
    .select('id, node_setup_tokens ( token )')
    .eq('node_code', body.node_code.toUpperCase())
    .maybeSingle()

  if (tokenLookupErr) {
    return new Response(JSON.stringify({ error: tokenLookupErr.message }), { status: 500 })
  }
  if (!tokenNode) {
    return new Response(JSON.stringify({ error: 'Node not found' }), { status: 404 })
  }
  const storedToken: string | undefined =
    (tokenNode as { node_setup_tokens?: { token?: string } | { token?: string }[] })
      .node_setup_tokens instanceof Array
      ? ((tokenNode as { node_setup_tokens: { token?: string }[] }).node_setup_tokens[0]?.token)
      : ((tokenNode as { node_setup_tokens?: { token?: string } }).node_setup_tokens?.token)

  // Nodes running pre-token firmware have no stored token yet; for those we
  // keep legacy behavior (code-only). As soon as the device heartbeats once
  // on new firmware, the token exists and becomes mandatory.
  if (storedToken && storedToken !== body.setup_token) {
    return new Response(JSON.stringify({ error: 'Invalid or missing setup token. Scan the QR code shown in your device\'s settings.' }), { status: 403 })
  }

  const { data, error } = await supabase
    .from('nodes')
    .update(updates)
    .eq('node_code', body.node_code.toUpperCase())
    .select('node_code, display_name, bio, wallet_address, twitter_handle, country, city, temp_unit, date_format, time_format, alarm_hour, alarm_minute, alarm_enabled, alarm_volume, screen_brightness, screen_always_on, screen_timeout_mins, nft_wallet_address, nft_grid_size, nft_carousel_enabled, nft_slideshow_secs, nft_pinlist, screen_order, screen_hidden, nft_show_data, nft_coll_order, nft_coll_hidden, nft_collections, ticker_cols, screen_carousel, screen_carousel_secs')
    .single()

  if (error) {
    return new Response(JSON.stringify({ error: error.message }), { status: 500 })
  }

  return new Response(JSON.stringify({ node: data }), {
    status: 200,
    headers: { 'Content-Type': 'application/json' },
  })
}

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

Deno.serve(async (req: Request) => {
  if (req.method !== 'POST') {
    return new Response(JSON.stringify({ error: 'Method not allowed' }), { status: 405 })
  }

  let body: {
    node_code?:              string
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
    // NFT Gallery settings
    nft_wallet_address?:     string
    nft_grid_size?:          1 | 4 | 9
    nft_carousel_enabled?:   boolean
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
  if (body.display_name  !== undefined) updates.display_name  = body.display_name.trim()
  if (body.bio           !== undefined) updates.bio           = body.bio.trim()
  if (body.wallet_address !== undefined) updates.wallet_address = body.wallet_address.trim()
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

  // NFT Gallery fields
  if (body.nft_wallet_address !== undefined) {
    const nw = body.nft_wallet_address.trim()
    if (nw !== '' && !EVM_ADDRESS_RE.test(nw))
      return new Response(JSON.stringify({ error: 'nft_wallet_address must be a valid 0x EVM address' }), { status: 400 })
    updates.nft_wallet_address = nw || null
  }
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
  if (body.screen_order !== undefined) {
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
      // Each item: chain:0xcontract:tokenId  (chain = lowercase letters, contract = 0x + 40 hex, tokenId = digits)
      const validItem = /^[a-z]+:0x[0-9a-f]{40}:[0-9]+$/i
      for (const item of pinItems) {
        if (!validItem.test(item))
          return new Response(JSON.stringify({ error: `Invalid nft_pinlist item: "${item}". Expected chain:0xcontract:tokenId` }), { status: 400 })
      }
      updates.nft_pinlist = pinItems.join(',')
    }
  }

  if (Object.keys(updates).length === 0) {
    return new Response(JSON.stringify({ error: 'No fields to update' }), { status: 400 })
  }

  const supabase = createClient(supabaseUrl, serviceRoleKey)

  const { data, error } = await supabase
    .from('nodes')
    .update(updates)
    .eq('node_code', body.node_code.toUpperCase())
    .select('node_code, display_name, bio, wallet_address, twitter_handle, country, city, temp_unit, date_format, time_format, alarm_hour, alarm_minute, alarm_enabled, alarm_volume, screen_brightness, screen_always_on, screen_timeout_mins, nft_wallet_address, nft_grid_size, nft_carousel_enabled, nft_slideshow_secs, nft_pinlist, screen_order')
    .single()

  if (error) {
    return new Response(JSON.stringify({ error: error.message }), { status: 500 })
  }

  return new Response(JSON.stringify({ node: data }), {
    status: 200,
    headers: { 'Content-Type': 'application/json' },
  })
})

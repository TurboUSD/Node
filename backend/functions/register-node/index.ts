// supabase/functions/register-node/index.ts
//
// Called once by a device on its very first successful WiFi connection.
// Creates the node row if the MAC address has never been seen before;
// if it has (e.g. the device was re-flashed), returns the existing record
// instead of creating a duplicate.
//
// IP geolocation: on first registration we call ip-api.com (free, no key
// required) to get city-level lat/lng/country from the device's public IP.
// This is stored once and never changes unless the user updates their
// country/city from the web setup page. Precision is city-level (~5–50 km),
// not street-level — appropriate for a public map.
//
// Required DB columns (run before deploying):
//   ALTER TABLE nodes ADD COLUMN IF NOT EXISTS lat double precision;
//   ALTER TABLE nodes ADD COLUMN IF NOT EXISTS lng double precision;
//
// Required view update — add lat and lng to public_node_directory:
//   CREATE OR REPLACE VIEW public_node_directory AS
//     SELECT node_code, display_name, bio, is_verified, is_genesis,
//            (last_seen_at > now() - interval '10 minutes') AS is_online,
//            total_tusd_earned, blocks_won, windows_online, uptime_pct,
//            created_at, last_seen_at, twitter_handle,
//            country, city, lat, lng
//     FROM nodes;
//   (Adapt to match your actual view definition — the key change is adding lat, lng.)
//
// Deploy with: supabase functions deploy register-node

import { createClient } from 'https://esm.sh/@supabase/supabase-js@2'

const supabaseUrl = Deno.env.get('SUPABASE_URL')!
const serviceRoleKey = Deno.env.get('SUPABASE_SERVICE_ROLE_KEY')!

// City-level geolocation via ip-api.com (free, ~45 req/min).
// Returns null gracefully on any error so registration never fails
// because the geo service is down.
interface GeoResult { lat: number; lng: number; country: string; city: string }

async function geolocateIp(ip: string): Promise<GeoResult | null> {
  // Skip private / loopback addresses — they won't resolve to anything useful
  if (!ip || /^(127\.|10\.|172\.(1[6-9]|2\d|3[01])\.|192\.168\.|::1$)/.test(ip)) return null
  try {
    const res = await fetch(
      `http://ip-api.com/json/${encodeURIComponent(ip)}?fields=status,country,city,lat,lon`,
      { signal: AbortSignal.timeout(3000) }
    )
    if (!res.ok) return null
    const d = await res.json()
    if (d.status !== 'success' || !d.lat || !d.lon) return null
    return { lat: d.lat, lng: d.lon, country: d.country ?? '', city: d.city ?? '' }
  } catch {
    return null
  }
}

function getClientIp(req: Request): string {
  return (
    req.headers.get('x-forwarded-for')?.split(',')[0]?.trim() ??
    req.headers.get('x-real-ip') ??
    ''
  )
}

function generateNodeCode(): string {
  // Short, human-friendly code shown on-device and in URLs, e.g. "A3F2"
  const chars = '0123456789ABCDEF'
  let code = ''
  for (let i = 0; i < 4; i++) code += chars[Math.floor(Math.random() * chars.length)]
  return code
}

Deno.serve(async (req: Request) => {
  if (req.method !== 'POST') {
    return new Response(JSON.stringify({ error: 'Method not allowed' }), { status: 405 })
  }

  let body: { mac_address?: string; firmware_version?: string }
  try {
    body = await req.json()
  } catch {
    return new Response(JSON.stringify({ error: 'Invalid JSON body' }), { status: 400 })
  }

  const macAddress = body.mac_address?.trim().toUpperCase()
  if (!macAddress || !/^([0-9A-F]{2}:){5}[0-9A-F]{2}$/.test(macAddress)) {
    return new Response(JSON.stringify({ error: 'A valid mac_address is required, e.g. AA:BB:CC:DD:EE:FF' }), { status: 400 })
  }

  const supabase = createClient(supabaseUrl, serviceRoleKey)

  // Already registered? Return the existing record (idempotent on re-flash).
  const { data: existing, error: lookupError } = await supabase
    .from('nodes')
    .select('id, node_code, display_name, is_verified, created_at')
    .eq('mac_address', macAddress)
    .maybeSingle()

  if (lookupError) {
    return new Response(JSON.stringify({ error: lookupError.message }), { status: 500 })
  }

  if (existing) {
    return new Response(JSON.stringify({ node: existing, created: false }), {
      status: 200,
      headers: { 'Content-Type': 'application/json' },
    })
  }

  // Generate a node_code that doesn't collide with an existing one.
  let nodeCode = generateNodeCode()
  for (let attempt = 0; attempt < 5; attempt++) {
    const { data: collision } = await supabase
      .from('nodes')
      .select('id')
      .eq('node_code', nodeCode)
      .maybeSingle()
    if (!collision) break
    nodeCode = generateNodeCode()
  }

  // Geolocate the device's public IP to pre-fill country/city/lat/lng.
  // Runs in parallel with nothing else (no await until the insert), so it
  // adds at most ~3 s to the registration time and never blocks on failure.
  const clientIp = getClientIp(req)
  const geo = await geolocateIp(clientIp)

  const { data: created, error: insertError } = await supabase
    .from('nodes')
    .insert({
      mac_address:      macAddress,
      node_code:        nodeCode,
      firmware_version: body.firmware_version ?? 'unknown',
      // Geolocation — only set if detected; user can override from setup page
      ...(geo && {
        lat:     geo.lat,
        lng:     geo.lng,
        country: geo.country,
        city:    geo.city,
      }),
    })
    .select('id, node_code, display_name, is_verified, created_at')
    .single()

  if (insertError) {
    return new Response(JSON.stringify({ error: insertError.message }), { status: 500 })
  }

  return new Response(JSON.stringify({ node: created, created: true }), {
    status: 201,
    headers: { 'Content-Type': 'application/json' },
  })
})

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

// Node code is DERIVED FROM THE DEVICE MAC (its last two octets) so it matches
// the "TurboUSD-Setup-XXXX" WiFi hotspot the device shows during setup — the
// firmware builds that hotspot name from the same two MAC bytes (mac[4]mac[5]).
// e.g. MAC ...:31:E8 → hotspot "TurboUSD-Setup-31E8" → node_code "31E8".
// (Previously this was random, so the setup hotspot and the final node code
// never matched, which was confusing.)
function nodeCodeFromMac(mac: string): string {
  const octets = mac.split(':')                 // ["AA","BB","CC","DD","EE","FF"]
  return (octets[4] + octets[5]).toUpperCase()  // last two octets → "EEFF"
}

// Deterministic fallback for the rare case two devices share the same last two
// MAC bytes: fold the whole MAC into a different 4-hex code so we never store a
// duplicate. (This variant won't match the hotspot name, but such collisions
// are rare — only ~1 in 65536 devices.)
function nodeCodeFallback(mac: string, salt: number): string {
  const hex = mac.replace(/:/g, '')
  let h = (salt * 0x9e3779b1) >>> 0
  for (let i = 0; i < hex.length; i++) h = (Math.imul(h, 31) + hex.charCodeAt(i)) >>> 0
  return h.toString(16).toUpperCase().padStart(8, '0').slice(-4)
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
    // Refresh the owner setup token for re-registrations too (e.g. after an
    // NVS wipe the device generates a NEW token — the QR on the device must
    // always be the one that works).
    if (body.setup_token && /^[0-9a-f]{8,64}$/i.test(body.setup_token)) {
      await supabase
        .from('node_setup_tokens')
        .upsert({ node_id: existing.id, token: body.setup_token, updated_at: new Date().toISOString() }, { onConflict: 'node_id' })
    }
    return new Response(JSON.stringify({ node: existing, created: false }), {
      status: 200,
      headers: { 'Content-Type': 'application/json' },
    })
  }

  // Derive the node_code from the MAC so it matches the setup hotspot name.
  // Fall back to a hashed variant only if that code is already taken by another
  // device (rare), so we never store a duplicate.
  let nodeCode = nodeCodeFromMac(macAddress)
  for (let attempt = 0; attempt < 6; attempt++) {
    const { data: collision } = await supabase
      .from('nodes')
      .select('id')
      .eq('node_code', nodeCode)
      .maybeSingle()
    if (!collision) break
    nodeCode = nodeCodeFallback(macAddress, attempt + 1)
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
      // PRIVACY: we NEVER store a node's real location. The IP-derived lat/lng
      // are snapped to a 3-degree grid (~300 km, country level) BEFORE they
      // touch the database, so the precise position is discarded here and only
      // the coarse dot ever exists. Country (already coarse) is kept for the
      // card/map. City is NEVER auto-filled — it's a manual profile field. The
      // owner can override country/city from the setup page at any time.
      ...(geo && {
        lat:     Math.round(geo.lat / 3) * 3,
        lng:     Math.round(geo.lng / 3) * 3,
        country: geo.country,
      }),
    })
    .select('id, node_code, display_name, is_verified, created_at')
    .single()

  if (insertError) {
    return new Response(JSON.stringify({ error: insertError.message }), { status: 500 })
  }

  // Store the device's owner setup token (best-effort — see heartbeat).
  if (body.setup_token && /^[0-9a-f]{8,64}$/i.test(body.setup_token)) {
    await supabase
      .from('node_setup_tokens')
      .upsert({ node_id: created.id, token: body.setup_token, updated_at: new Date().toISOString() }, { onConflict: 'node_id' })
  }

  return new Response(JSON.stringify({ node: created, created: true }), {
    status: 201,
    headers: { 'Content-Type': 'application/json' },
  })
})

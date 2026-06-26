// supabase/functions/heartbeat/index.ts
//
// Called periodically by every device (e.g. every 2-5 minutes) while it has
// network connectivity. Updates last_seen_at, which is what determines
// whether a node counts as "online" / an active mining candidate.
//
// Also returns config so the device can apply any changes made via the web
// setup page without a separate config fetch.
//
// Geolocation backfill: if the node has no lat/lng (registered before
// geolocation was added, or registration geo call failed), we detect it once
// on heartbeat from the device's public IP. We skip on all subsequent
// heartbeats once lat is set.
//
// Deploy with: supabase functions deploy heartbeat

import { createClient } from 'https://esm.sh/@supabase/supabase-js@2'

const supabaseUrl = Deno.env.get('SUPABASE_URL')!
const serviceRoleKey = Deno.env.get('SUPABASE_SERVICE_ROLE_KEY')!

interface GeoResult { lat: number; lng: number; country: string; city: string }

async function geolocateIp(ip: string): Promise<GeoResult | null> {
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

Deno.serve(async (req: Request) => {
  if (req.method !== 'POST') {
    return new Response(JSON.stringify({ error: 'Method not allowed' }), { status: 405 })
  }

  let body: {
    mac_address?: string
    uptime_seconds?: number
    wifi_rssi?: number
    free_heap_bytes?: number
  }
  try {
    body = await req.json()
  } catch {
    return new Response(JSON.stringify({ error: 'Invalid JSON body' }), { status: 400 })
  }

  const macAddress = body.mac_address?.trim().toUpperCase()
  if (!macAddress) {
    return new Response(JSON.stringify({ error: 'mac_address is required' }), { status: 400 })
  }

  const supabase = createClient(supabaseUrl, serviceRoleKey)

  const { data: node, error: lookupError } = await supabase
    .from('nodes')
    .select(`
      id, lat,
      temp_unit, date_format, time_format,
      alarm_hour, alarm_minute, alarm_enabled, alarm_volume,
      nft_wallet_address, nft_grid_size, nft_carousel_enabled, nft_slideshow_secs,
      nft_pinlist, screen_order
    `)
    .eq('mac_address', macAddress)
    .maybeSingle()

  if (lookupError) {
    return new Response(JSON.stringify({ error: lookupError.message }), { status: 500 })
  }
  if (!node) {
    // Device thinks it's registered but the server has no record (e.g. DB was
    // reset). Tell it explicitly so the firmware can re-run registration.
    return new Response(JSON.stringify({ error: 'Unknown node, please re-register' }), { status: 404 })
  }

  const now = new Date().toISOString()

  // Geolocation backfill: detect once for nodes registered before geo was added
  const geoUpdate: Record<string, unknown> = { last_seen_at: now }
  if (node.lat == null) {
    const clientIp =
      req.headers.get('x-forwarded-for')?.split(',')[0]?.trim() ??
      req.headers.get('x-real-ip') ?? ''
    const geo = await geolocateIp(clientIp)
    if (geo) {
      geoUpdate.lat     = geo.lat
      geoUpdate.lng     = geo.lng
      // Only set country/city if the node doesn't already have them set manually
      geoUpdate.country = geo.country
      geoUpdate.city    = geo.city
    }
  }

  const { error: updateError } = await supabase
    .from('nodes')
    .update(geoUpdate)
    .eq('id', node.id)

  if (updateError) {
    return new Response(JSON.stringify({ error: updateError.message }), { status: 500 })
  }

  const { error: insertError } = await supabase.from('node_heartbeats').insert({
    node_id: node.id,
    received_at: now,
    uptime_seconds: body.uptime_seconds ?? null,
    wifi_rssi: body.wifi_rssi ?? null,
    free_heap_bytes: body.free_heap_bytes ?? null,
  })

  if (insertError) {
    return new Response(JSON.stringify({ error: insertError.message }), { status: 500 })
  }

  // Return config so the device can sync its NVS on every heartbeat.
  // Fields may be null if not yet set by the owner via the web setup page —
  // the firmware should treat null as "keep current NVS value, do not overwrite".
  const config = {
    temp_unit:             node.temp_unit             ?? null,
    date_format:           node.date_format           ?? null,
    time_format:           node.time_format           ?? null,
    alarm_hour:            node.alarm_hour            ?? null,
    alarm_minute:          node.alarm_minute          ?? null,
    alarm_enabled:         node.alarm_enabled         ?? null,
    alarm_volume:          node.alarm_volume          ?? null,
    nft_wallet_address:    node.nft_wallet_address    ?? null,
    nft_grid_size:         node.nft_grid_size         ?? null,
    nft_carousel_enabled:  node.nft_carousel_enabled  ?? null,
    nft_slideshow_secs:    node.nft_slideshow_secs    ?? null,
    nft_pinlist:           node.nft_pinlist           ?? null,
    screen_order:          node.screen_order          ?? null,
  }

  return new Response(JSON.stringify({ ok: true, config }), {
    status: 200,
    headers: { 'Content-Type': 'application/json' },
  })
})

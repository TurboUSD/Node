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
    setup_token?: string    // per-device owner secret shown in the Settings QR
    // Device-side alarm change being pushed up (see firmware storage.h
    // alarm_dirty). Applied to the nodes row BEFORE the config is built, so
    // the response already reflects it.
    alarm_hour?: number
    alarm_minute?: number
    alarm_enabled?: boolean
    alarm_days?: number
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
      id, lat, display_name, is_verified,
      uptime_seconds, total_uptime_seconds,
      temp_unit, date_format, time_format,
      alarm_hour, alarm_minute, alarm_enabled, alarm_volume,
      screen_brightness, screen_always_on, screen_timeout_mins,
      nft_wallet_address, nft_grid_size, nft_carousel_enabled, nft_slideshow_secs,
      nft_pinlist, screen_order, screen_hidden,
      nft_show_data, nft_coll_order, nft_coll_hidden, ticker_cols
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

  // Device-pushed alarm (set on the physical device since the last sync).
  if (body.alarm_hour !== undefined || body.alarm_minute !== undefined ||
      body.alarm_enabled !== undefined || body.alarm_days !== undefined) {
    const alarmUpd: Record<string, unknown> = {}
    if (body.alarm_hour    !== undefined) alarmUpd.alarm_hour    = body.alarm_hour
    if (body.alarm_minute  !== undefined) alarmUpd.alarm_minute  = body.alarm_minute
    if (body.alarm_enabled !== undefined) alarmUpd.alarm_enabled = body.alarm_enabled
    if (body.alarm_days    !== undefined) alarmUpd.alarm_days    = body.alarm_days
    await supabase.from('nodes').update(alarmUpd).eq('id', node.id)
    // Reflect in the in-memory copy so the config below echoes the new values.
    Object.assign(node as Record<string, unknown>, alarmUpd)
  }

  // Device-pushed NFT gallery state: the detected collections list (the web
  // setup page renders it as a board) and, when edited ON the device (gear
  // mode / Data toggle), the order/hidden/show_data lists.
  {
    const nftUpd: Record<string, unknown> = {}
    if (body.nft_collections !== undefined) nftUpd.nft_collections = body.nft_collections
    if (body.nft_coll_order  !== undefined) nftUpd.nft_coll_order  = body.nft_coll_order
    if (body.nft_coll_hidden !== undefined) nftUpd.nft_coll_hidden = body.nft_coll_hidden
    if (body.nft_show_data   !== undefined) nftUpd.nft_show_data   = body.nft_show_data
    if (Object.keys(nftUpd).length > 0) {
      await supabase.from('nodes').update(nftUpd).eq('id', node.id)
      Object.assign(node as Record<string, unknown>, nftUpd)
    }
  }

  // Geolocation backfill: detect once for nodes registered before geo was added
  const geoUpdate: Record<string, unknown> = {
    last_seen_at: now,
    // Latest device-reported uptime (seconds since boot) — the node cards on
    // the web show this exact figure so device & web always agree.
    ...(body.uptime_seconds !== undefined ? { uptime_seconds: body.uptime_seconds } : {}),
  }
  // Cumulative uptime: since-boot uptime resets on every reboot, so we
  // accumulate the DELTA between consecutive reports into
  // total_uptime_seconds. If the new value is smaller than the previous one
  // the device rebooted — the whole new since-boot value is fresh runtime.
  if (body.uptime_seconds !== undefined) {
    const prev = Number((node as Record<string, unknown>).uptime_seconds ?? 0)
    const cur  = Number(body.uptime_seconds)
    const delta = cur >= prev ? cur - prev : cur
    geoUpdate.total_uptime_seconds =
      Number((node as Record<string, unknown>).total_uptime_seconds ?? 0) + Math.max(0, delta)
  }
  if (node.lat == null) {
    const clientIp =
      req.headers.get('x-forwarded-for')?.split(',')[0]?.trim() ??
      req.headers.get('x-real-ip') ?? ''
    const geo = await geolocateIp(clientIp)
    if (geo) {
      geoUpdate.lat = geo.lat
      geoUpdate.lng = geo.lng
      // Country only, and only when missing. NEVER auto-write city: it kept
      // reappearing in profiles after the owner deleted it (the profile city
      // is a manual, payout-related field — geo-IP has no business there).
      if (!node.country) geoUpdate.country = geo.country
    }
  }

  const { error: updateError } = await supabase
    .from('nodes')
    .update(geoUpdate)
    .eq('id', node.id)

  if (updateError) {
    return new Response(JSON.stringify({ error: updateError.message }), { status: 500 })
  }

  // Keep the server-side copy of the device's setup token fresh. This is how
  // nodes registered BEFORE tokens existed get one stored (the device is the
  // source of truth; requests are already authenticated by MAC knowledge the
  // same way the rest of the heartbeat is). Best-effort: a failure here must
  // not break heartbeats (e.g. if the migration hasn't been run yet).
  if (body.setup_token && /^[0-9a-f]{8,64}$/i.test(body.setup_token)) {
    await supabase
      .from('node_setup_tokens')
      .upsert({ node_id: node.id, token: body.setup_token, updated_at: now }, { onConflict: 'node_id' })
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
  // Total ₸ earned (for the Node screen headline). Best-effort join.
  let totalEarned = 0
  {
    const { data: bal } = await supabase
      .from('node_reward_balances')
      .select('total_tusd_earned')
      .eq('node_id', node.id)
      .maybeSingle()
    totalEarned = Number(bal?.total_tusd_earned ?? 0)
  }

  const config = {
    display_name:          node.display_name          ?? null,
    is_verified:           node.is_verified           ?? false,
    total_tusd_earned:     totalEarned,
    // Cumulative uptime across ALL reboots (server-accumulated just above), so
    // the device can show lifetime uptime instead of its since-boot millis.
    total_uptime_seconds:  (geoUpdate.total_uptime_seconds
                            ?? (node as Record<string, unknown>).total_uptime_seconds
                            ?? null),
    temp_unit:             node.temp_unit             ?? null,
    date_format:           node.date_format           ?? null,
    time_format:           node.time_format           ?? null,
    alarm_hour:            node.alarm_hour            ?? null,
    alarm_minute:          node.alarm_minute          ?? null,
    alarm_enabled:         node.alarm_enabled         ?? null,
    alarm_volume:          node.alarm_volume          ?? null,
    screen_brightness:     node.screen_brightness     ?? null,
    screen_always_on:      node.screen_always_on      ?? null,
    screen_timeout_mins:   node.screen_timeout_mins   ?? null,
    nft_wallet_address:    node.nft_wallet_address    ?? null,
    nft_grid_size:         node.nft_grid_size         ?? null,
    nft_carousel_enabled:  node.nft_carousel_enabled  ?? null,
    nft_show_data:         node.nft_show_data         ?? null,
    nft_coll_order:        node.nft_coll_order        ?? null,
    nft_coll_hidden:       node.nft_coll_hidden       ?? null,
    screen_hidden:         node.screen_hidden         ?? null,
    ticker_cols:           node.ticker_cols           ?? null,
    nft_slideshow_secs:    node.nft_slideshow_secs    ?? null,
    nft_pinlist:           node.nft_pinlist           ?? null,
    screen_order:          node.screen_order          ?? null,
  }

  return new Response(JSON.stringify({ ok: true, config }), {
    status: 200,
    headers: { 'Content-Type': 'application/json' },
  })
})

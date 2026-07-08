// supabase/functions/latest-firmware/index.ts
//
// GET-style endpoint the device polls (e.g. once a day, or on every boot)
// to check for OTA updates. Query string: ?target=esp32s3 or ?target=rp2040
//
// Returns the newest *active* release for that target. The device compares
// `version` against its own compiled-in version string and, if newer,
// downloads `binary_url`, verifies it against `sha256`, and only then
// flashes it to the inactive partition (see the OTA firmware doc for the
// ESP32-S3 side of this).
//
// Deploy with: supabase functions deploy latest-firmware --no-verify-jwt
// (no-verify-jwt because devices call this anonymously, before they have
// any session -- it only ever returns public release metadata)

import { createClient } from 'https://esm.sh/@supabase/supabase-js@2'

const supabaseUrl = Deno.env.get('SUPABASE_URL')!
const anonKey = Deno.env.get('SUPABASE_ANON_KEY')!

// The web setup page (network.turbousd.com) calls this from the browser, so it
// needs CORS. Without it the browser blocks the response and the fetch fails
// with a bare "Failed to fetch" (the setup page's "Check for updates" button).
// The device (ESP32) ignores these headers — they only matter for browsers.
const CORS = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Headers': 'authorization, x-client-info, apikey, content-type',
  'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
}

Deno.serve(async (req: Request) => {
  if (req.method === 'OPTIONS') return new Response('ok', { headers: CORS })

  const url = new URL(req.url)
  const target = url.searchParams.get('target')

  if (target !== 'esp32s3' && target !== 'rp2040') {
    return new Response(JSON.stringify({ error: 'target query param must be "esp32s3" or "rp2040"' }), { status: 400, headers: { ...CORS, 'Content-Type': 'application/json' } })
  }

  const supabase = createClient(supabaseUrl, anonKey)

  const { data, error } = await supabase
    .from('latest_firmware')
    .select('version, binary_url, sha256, release_notes, published_at')
    .eq('target', target)
    .maybeSingle()

  if (error) {
    return new Response(JSON.stringify({ error: error.message }), { status: 500, headers: { ...CORS, 'Content-Type': 'application/json' } })
  }
  if (!data) {
    return new Response(JSON.stringify({ error: 'No active release found for this target' }), { status: 404, headers: { ...CORS, 'Content-Type': 'application/json' } })
  }

  return new Response(JSON.stringify(data), {
    status: 200,
    headers: { ...CORS, 'Content-Type': 'application/json', 'Cache-Control': 'public, max-age=300' },
  })
})

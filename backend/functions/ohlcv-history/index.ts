// supabase/functions/ohlcv-history/index.ts
//
// GET-style endpoint the device calls to populate the Turbo Stats screen's
// candle chart. Returns up to 26 most recent weekly candles from the
// tusd_ohlcv_history cache (synced daily by sync-ohlcv-history).
//
// Deploy with: supabase functions deploy ohlcv-history --no-verify-jwt

import { createClient } from 'https://esm.sh/@supabase/supabase-js@2'

const supabaseUrl = Deno.env.get('SUPABASE_URL')!
const anonKey = Deno.env.get('SUPABASE_ANON_KEY')!

Deno.serve(async (_req: Request) => {
  const supabase = createClient(supabaseUrl, anonKey)

  const { data, error } = await supabase
    .from('tusd_ohlcv_history')
    .select('candle_open_time, open_usd, high_usd, low_usd, close_usd')
    .order('candle_open_time', { ascending: true })
    .limit(26)

  if (error) {
    return new Response(JSON.stringify({ error: error.message }), { status: 500 })
  }

  return new Response(JSON.stringify({ candles: data }), {
    status: 200,
    headers: { 'Content-Type': 'application/json', 'Cache-Control': 'public, max-age=3600' },
  })
})

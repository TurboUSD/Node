// supabase/functions/sync-ohlcv-history/index.ts
//
// Run daily by a Supabase Cron Job. Pulls weekly OHLCV candles for the
// TUSD/Base pool from GeckoTerminal and upserts them into
// tusd_ohlcv_history. Devices read that cache (via the ohlcv-history
// function below), never GeckoTerminal directly -- see that table's
// comment for why.
//
// IMPORTANT LIMITATION: GeckoTerminal's free public API only returns up
// to ~6 months of history per call (confirmed in their docs). This means
// on first run you'll only get recent candles, not the pool's full
// history back to creation. Real history accumulates naturally over time
// as this runs daily and each new week's candle gets added -- there's no
// way to backfill further back than 6 months on the free tier short of
// upgrading to GeckoTerminal/CoinGecko's paid plan.
//
// Deploy with: supabase functions deploy sync-ohlcv-history
// Schedule with: select cron.schedule('sync-ohlcv-history', '30 6 * * *',
//   $$ select net.http_post(url:='https://<project>.functions.supabase.co/sync-ohlcv-history') $$);

import { createClient } from 'https://esm.sh/@supabase/supabase-js@2'

const supabaseUrl = Deno.env.get('SUPABASE_URL')!
const serviceRoleKey = Deno.env.get('SUPABASE_SERVICE_ROLE_KEY')!

const NETWORK = 'base'
const POOL_ADDRESS = '0xd013725b904e76394a3ab0334da306c505d778f8'
const GECKOTERMINAL_URL = `https://api.geckoterminal.com/api/v2/networks/${NETWORK}/pools/${POOL_ADDRESS}/ohlcv/week?aggregate=1&currency=usd&limit=100`

Deno.serve(async (_req: Request) => {
  const supabase = createClient(supabaseUrl, serviceRoleKey)

  const res = await fetch(GECKOTERMINAL_URL, { headers: { Accept: 'application/json' } })
  if (!res.ok) {
    return new Response(JSON.stringify({ error: `GeckoTerminal API returned ${res.status}` }), { status: 500 })
  }

  const json = await res.json()
  const ohlcvList: number[][] = json?.data?.attributes?.ohlcv_list ?? []

  if (ohlcvList.length === 0) {
    return new Response(JSON.stringify({ ok: true, rows_upserted: 0, note: 'GeckoTerminal returned no candles' }), { status: 200 })
  }

  const rows = ohlcvList.map(([ts, open, high, low, close, volume]) => ({
    candle_open_time: new Date(ts * 1000).toISOString(),
    open_usd: open,
    high_usd: high,
    low_usd: low,
    close_usd: close,
    volume_usd: volume,
  }))

  const { error } = await supabase.from('tusd_ohlcv_history').upsert(rows, { onConflict: 'candle_open_time' })
  if (error) {
    return new Response(JSON.stringify({ error: error.message }), { status: 500 })
  }

  return new Response(JSON.stringify({ ok: true, rows_upserted: rows.length }), {
    status: 200,
    headers: { 'Content-Type': 'application/json' },
  })
})

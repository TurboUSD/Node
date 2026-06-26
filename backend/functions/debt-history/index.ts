// supabase/functions/debt-history/index.ts
//
// GET-style endpoint the device calls when the user changes the debt
// chart's year-range selector. Query string: ?years=50
//
// Returns ~40 evenly-spaced points covering that many years, read from the
// us_debt_history cache table (synced daily by sync-debt-history) rather
// than calling Treasury directly -- see that function's header comment
// for why.
//
// Deploy with: supabase functions deploy debt-history --no-verify-jwt

import { createClient } from 'https://esm.sh/@supabase/supabase-js@2'

const supabaseUrl = Deno.env.get('SUPABASE_URL')!
const anonKey = Deno.env.get('SUPABASE_ANON_KEY')!

Deno.serve(async (req: Request) => {
  const url = new URL(req.url)
  const years = parseInt(url.searchParams.get('years') ?? '50', 10)

  if (!Number.isFinite(years) || years <= 0 || years > 100) {
    return new Response(JSON.stringify({ error: 'years must be a number between 1 and 100' }), { status: 400 })
  }

  const supabase = createClient(supabaseUrl, anonKey)

  const { data, error } = await supabase.rpc('debt_history_sampled', { years_back: years, max_points: 40 })

  if (error) {
    return new Response(JSON.stringify({ error: error.message }), { status: 500 })
  }

  return new Response(JSON.stringify({ years, points: data }), {
    status: 200,
    headers: { 'Content-Type': 'application/json', 'Cache-Control': 'public, max-age=3600' },
  })
})

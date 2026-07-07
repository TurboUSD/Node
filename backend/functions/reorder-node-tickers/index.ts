// supabase/functions/reorder-node-tickers/index.ts
//
// Persists a node's on-device ticker reordering (edit mode ▲▼ on the Tickers
// screen). Body: { node_code, pool_addresses: string[] } — the array is the
// FULL list in the new display order; each pool gets display_order = index.
//
// Deploy: supabase functions deploy reorder-node-tickers

import { createClient } from 'https://esm.sh/@supabase/supabase-js@2'

const supabaseUrl    = Deno.env.get('SUPABASE_URL')!
const serviceRoleKey = Deno.env.get('SUPABASE_SERVICE_ROLE_KEY')!

const CORS = {
  'Access-Control-Allow-Origin':  '*',
  'Access-Control-Allow-Methods': 'POST, OPTIONS',
  'Access-Control-Allow-Headers': 'Content-Type, Authorization, apikey',
  'Content-Type': 'application/json',
}

Deno.serve(async (req: Request) => {
  if (req.method === 'OPTIONS') return new Response(null, { headers: CORS })

  const supabase = createClient(supabaseUrl, serviceRoleKey)

  let body: Record<string, unknown>
  try { body = await req.json() } catch {
    return new Response(JSON.stringify({ error: 'Invalid JSON' }), { status: 400, headers: CORS })
  }

  const { node_code, pool_addresses } = body as { node_code: string; pool_addresses: string[] }

  if (!node_code || !Array.isArray(pool_addresses) || pool_addresses.length === 0) {
    return new Response(JSON.stringify({ error: 'Missing node_code or pool_addresses' }), { status: 400, headers: CORS })
  }

  const { data: node, error: nodeErr } = await supabase
    .from('nodes')
    .select('id')
    .eq('node_code', node_code.toUpperCase())
    .eq('is_active', true)
    .maybeSingle()

  if (nodeErr || !node) {
    return new Response(JSON.stringify({ error: 'Node not found' }), { status: 404, headers: CORS })
  }

  // One update per pool — the list is at most 10 entries, so this stays tiny.
  for (let i = 0; i < pool_addresses.length; i++) {
    const { error: updErr } = await supabase
      .from('node_tickers')
      .update({ display_order: i })
      .eq('node_id', node.id)
      .eq('pool_address', String(pool_addresses[i]).toLowerCase())

    if (updErr) {
      return new Response(JSON.stringify({ error: updErr.message }), { status: 500, headers: CORS })
    }
  }

  return new Response(JSON.stringify({ ok: true }), { headers: CORS })
})

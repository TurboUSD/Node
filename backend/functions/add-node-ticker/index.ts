// supabase/functions/add-node-ticker/index.ts
//
// Saves a ticker (DexScreener pair) to a node's watchlist.
// Called from setup page after user picks from search results.
//
// Body: { node_code, pool_address, chain_id, base_symbol, base_name, quote_symbol, display_order? }
// Auth: uses node_code as ownership proof (same pattern as update-node-config)
//
// Deploy: supabase functions deploy add-node-ticker

import { createClient } from 'https://esm.sh/@supabase/supabase-js@2'

const supabaseUrl     = Deno.env.get('SUPABASE_URL')!
const serviceRoleKey  = Deno.env.get('SUPABASE_SERVICE_ROLE_KEY')!

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

  const { node_code, pool_address, chain_id, base_symbol, base_name, quote_symbol, display_order } = body as {
    node_code:     string
    pool_address:  string
    chain_id:      string
    base_symbol:   string
    base_name:     string
    quote_symbol?: string
    display_order?: number
  }

  if (!node_code || !pool_address || !chain_id || !base_symbol || !base_name) {
    return new Response(JSON.stringify({ error: 'Missing required fields' }), { status: 400, headers: CORS })
  }

  // Resolve node
  const { data: node, error: nodeErr } = await supabase
    .from('nodes')
    .select('id')
    .eq('node_code', node_code.toUpperCase())
    .eq('is_active', true)
    .maybeSingle()

  if (nodeErr || !node) {
    return new Response(JSON.stringify({ error: 'Node not found' }), { status: 404, headers: CORS })
  }

  // Upsert ticker (unique on node_id + pool_address)
  const { error: upsertErr } = await supabase
    .from('node_tickers')
    .upsert({
      node_id:      node.id,
      pool_address: pool_address.toLowerCase(),
      chain_id,
      base_symbol:  base_symbol.toUpperCase(),
      base_name,
      quote_symbol: (quote_symbol ?? 'USDC').toUpperCase(),
      display_order: display_order ?? 0,
      updated_at:   new Date().toISOString(),
    }, { onConflict: 'node_id,pool_address' })

  if (upsertErr) {
    const msg = upsertErr.message.includes('Maximum 10')
      ? 'You can save up to 10 tickers per node'
      : upsertErr.message
    return new Response(JSON.stringify({ error: msg }), { status: 400, headers: CORS })
  }

  return new Response(JSON.stringify({ ok: true }), { headers: CORS })
})

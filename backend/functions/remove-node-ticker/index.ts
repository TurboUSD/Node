// supabase/functions/remove-node-ticker/index.ts
//
// Removes a ticker from a node's watchlist.
// Body: { node_code, pool_address }
//
// Deploy: supabase functions deploy remove-node-ticker

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

  const { node_code, pool_address } = body as { node_code: string; pool_address: string }

  if (!node_code || !pool_address) {
    return new Response(JSON.stringify({ error: 'Missing node_code or pool_address' }), { status: 400, headers: CORS })
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

  const { error: delErr } = await supabase
    .from('node_tickers')
    .delete()
    .eq('node_id', node.id)
    .eq('pool_address', pool_address.toLowerCase())

  if (delErr) {
    return new Response(JSON.stringify({ error: delErr.message }), { status: 500, headers: CORS })
  }

  return new Response(JSON.stringify({ ok: true }), { headers: CORS })
})

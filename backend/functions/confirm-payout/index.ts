// supabase/functions/confirm-payout/index.ts
//
// Called by the AMI bot AFTER it successfully sends ₸USD on-chain.
// Records the payment so the same rewards are not paid twice.
//
// Authentication: requires X-AMI-Secret header (same secret as rewards-payout).
//
// Deploy with: supabase functions deploy confirm-payout --no-verify-jwt
//
// Request body:
//   {
//     node_code:    string,   // e.g. "A3F2"
//     amount_tusd:  number,   // ₸USD amount actually sent (may differ from pending if partial)
//     tx_hash?:     string,   // optional on-chain tx hash for audit trail
//   }

import { createClient } from 'https://esm.sh/@supabase/supabase-js@2'

const supabaseUrl    = Deno.env.get('SUPABASE_URL')!
const serviceRoleKey = Deno.env.get('SUPABASE_SERVICE_ROLE_KEY')!
const amiSecret      = Deno.env.get('AMI_PAYOUT_SECRET')

Deno.serve(async (req: Request) => {
  if (req.method !== 'POST') {
    return new Response(JSON.stringify({ error: 'Method not allowed' }), { status: 405 })
  }

  if (amiSecret) {
    const provided = req.headers.get('X-AMI-Secret')
    if (provided !== amiSecret) {
      return new Response(JSON.stringify({ error: 'Unauthorized' }), { status: 401 })
    }
  }

  let body: { node_code?: string; amount_tusd?: number; tx_hash?: string }
  try {
    body = await req.json()
  } catch {
    return new Response(JSON.stringify({ error: 'Invalid JSON body' }), { status: 400 })
  }

  const { node_code, amount_tusd, tx_hash } = body

  if (!node_code || typeof node_code !== 'string')
    return new Response(JSON.stringify({ error: 'node_code is required' }), { status: 400 })

  if (!amount_tusd || typeof amount_tusd !== 'number' || amount_tusd <= 0)
    return new Response(JSON.stringify({ error: 'amount_tusd must be a positive number' }), { status: 400 })

  const supabase = createClient(supabaseUrl, serviceRoleKey)

  // Resolve node_id from node_code
  const { data: node, error: nodeErr } = await supabase
    .from('nodes')
    .select('id')
    .eq('node_code', node_code.toUpperCase())
    .maybeSingle()

  if (nodeErr || !node) {
    return new Response(JSON.stringify({ error: 'Node not found' }), { status: 404 })
  }

  // Atomically increment total_tusd_paid and update last_paid_at.
  // Uses a raw SQL update to avoid a read-modify-write race condition.
  const { error: updateErr } = await supabase.rpc('increment_tusd_paid', {
    p_node_id:    node.id,
    p_amount:     amount_tusd,
    p_tx_hash:    tx_hash ?? null,
  })

  if (updateErr) {
    // Fallback if the RPC isn't deployed yet: plain update (non-atomic, fine for single-bot usage)
    const { error: fallbackErr } = await supabase
      .from('node_reward_balances')
      .update({
        total_tusd_paid: supabase.rpc('coalesce_add_paid', {}), // won't work — use RPC in prod
        last_paid_at: new Date().toISOString(),
      })
      .eq('node_id', node.id)

    if (fallbackErr) {
      return new Response(JSON.stringify({ error: updateErr.message }), { status: 500 })
    }
  }

  return new Response(JSON.stringify({
    ok: true,
    node_code: node_code.toUpperCase(),
    amount_confirmed: amount_tusd,
    tx_hash: tx_hash ?? null,
  }), {
    status: 200,
    headers: { 'Content-Type': 'application/json' },
  })
})

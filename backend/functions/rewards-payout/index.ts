// supabase/functions/rewards-payout/index.ts
//
// Read-only endpoint used by the AMI payout bot to fetch the list of
// nodes that have unpaid ₸USD rewards and a configured wallet address.
//
// The bot calls this, executes the on-chain transfers, then calls
// confirm-payout for each one to record the payment.
//
// Authentication: requires the custom header
//   X-AMI-Secret: <value of AMI_PAYOUT_SECRET env var>
// This keeps the wallet addresses and amounts private.
//
// Deploy with: supabase functions deploy rewards-payout --no-verify-jwt
//
// Response shape:
//   { payouts: Array<{
//       node_code:       string,
//       wallet_address:  string,
//       twitter_handle:  string | null,
//       display_name:    string | null,
//       pending_tusd:    number,   // ₸USD owed (earned - already paid)
//       total_earned:    number,
//       total_paid:      number,
//       last_paid_at:    string | null,
//   }> }

import { createClient } from 'https://esm.sh/@supabase/supabase-js@2'

const supabaseUrl    = Deno.env.get('SUPABASE_URL')!
const serviceRoleKey = Deno.env.get('SUPABASE_SERVICE_ROLE_KEY')!
const amiSecret      = Deno.env.get('AMI_PAYOUT_SECRET')   // set in Supabase dashboard secrets

Deno.serve(async (req: Request) => {
  // Only GET (or POST for bot convenience, payload ignored either way).
  if (req.method !== 'GET' && req.method !== 'POST') {
    return new Response(JSON.stringify({ error: 'Method not allowed' }), { status: 405 })
  }

  // Require AMI secret header if the env var is configured.
  if (amiSecret) {
    const provided = req.headers.get('X-AMI-Secret')
    if (provided !== amiSecret) {
      return new Response(JSON.stringify({ error: 'Unauthorized' }), { status: 401 })
    }
  }

  const supabase = createClient(supabaseUrl, serviceRoleKey)

  // pending_payouts view (defined in 006_node_profile_and_payouts.sql):
  // nodes with wallet, active status, and pending_tusd > 0
  const { data, error } = await supabase
    .from('pending_payouts')
    .select('node_code, wallet_address, twitter_handle, display_name, pending_tusd, total_tusd_earned, total_tusd_paid, last_paid_at')
    .order('pending_tusd', { ascending: false })

  if (error) {
    return new Response(JSON.stringify({ error: error.message }), { status: 500 })
  }

  const payouts = (data ?? []).map(row => ({
    node_code:      row.node_code,
    wallet_address: row.wallet_address,
    twitter_handle: row.twitter_handle ?? null,
    display_name:   row.display_name ?? null,
    pending_tusd:   Number(row.pending_tusd),
    total_earned:   Number(row.total_tusd_earned),
    total_paid:     Number(row.total_tusd_paid),
    last_paid_at:   row.last_paid_at ?? null,
  }))

  return new Response(JSON.stringify({ payouts, count: payouts.length }), {
    status: 200,
    headers: { 'Content-Type': 'application/json' },
  })
})

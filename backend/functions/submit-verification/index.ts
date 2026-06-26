// supabase/functions/submit-verification/index.ts
//
// Called from the web setup page when the owner submits their X post URL
// and wallet address as proof of physical ownership (see the verification
// flow we designed: video on X showing the node name written on paper,
// tagging @turbousd, plus the wallet that holds/will receive TUSD).
//
// This does NOT auto-verify anything -- it just records the submission.
// You review it manually and flip is_verified via the admin panel/SQL.
// That manual-review step is the actual trust boundary; this function's
// job is just to capture the claim cleanly.
//
// Deploy with: supabase functions deploy submit-verification

import { createClient } from 'https://esm.sh/@supabase/supabase-js@2'

const supabaseUrl = Deno.env.get('SUPABASE_URL')!
const serviceRoleKey = Deno.env.get('SUPABASE_SERVICE_ROLE_KEY')!

function isLikelyTweetUrl(url: string): boolean {
  return /^https:\/\/(x\.com|twitter\.com)\/[^/]+\/status\/\d+/.test(url)
}

function isLikelyEvmAddress(addr: string): boolean {
  return /^0x[0-9a-fA-F]{40}$/.test(addr)
}

Deno.serve(async (req: Request) => {
  if (req.method !== 'POST') {
    return new Response(JSON.stringify({ error: 'Method not allowed' }), { status: 405 })
  }

  let body: { node_code?: string; tweet_url?: string; wallet_address?: string }
  try {
    body = await req.json()
  } catch {
    return new Response(JSON.stringify({ error: 'Invalid JSON body' }), { status: 400 })
  }

  if (!body.node_code || !body.tweet_url || !body.wallet_address) {
    return new Response(JSON.stringify({ error: 'node_code, tweet_url, and wallet_address are all required' }), { status: 400 })
  }

  if (!isLikelyTweetUrl(body.tweet_url)) {
    return new Response(JSON.stringify({ error: 'tweet_url does not look like a valid X/Twitter status link' }), { status: 400 })
  }
  if (!isLikelyEvmAddress(body.wallet_address)) {
    return new Response(JSON.stringify({ error: 'wallet_address does not look like a valid EVM address' }), { status: 400 })
  }

  const supabase = createClient(supabaseUrl, serviceRoleKey)

  const { data, error } = await supabase
    .from('nodes')
    .update({
      verification_tweet_url: body.tweet_url,
      verification_wallet_address: body.wallet_address.toLowerCase(),
      // is_verified intentionally left untouched here -- a human reviews it next.
    })
    .eq('node_code', body.node_code.toUpperCase())
    .select('node_code, is_verified')
    .single()

  if (error) {
    return new Response(JSON.stringify({ error: error.message }), { status: 500 })
  }

  return new Response(JSON.stringify({ node: data, message: 'Submitted for manual review' }), {
    status: 200,
    headers: { 'Content-Type': 'application/json' },
  })
})

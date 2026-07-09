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

const CORS_HEADERS: Record<string, string> = {
  'Access-Control-Allow-Origin':  '*',
  'Access-Control-Allow-Methods': 'POST, OPTIONS',
  'Access-Control-Allow-Headers': 'Content-Type, Authorization, apikey',
}

// CORS wrapper: the browser preflights JSON POSTs with an OPTIONS request;
// without these headers every call from network.turbousd.com died in the
// browser as a generic "Failed to fetch" (the function itself was fine).
Deno.serve(async (req: Request) => {
  if (req.method === 'OPTIONS') return new Response(null, { headers: CORS_HEADERS })
  let res: Response
  try {
    res = await handle(req)
  } catch (err) {
    res = new Response(JSON.stringify({ error: `Internal error: ${(err as Error).message}` }), { status: 500 })
  }
  const headers = new Headers(res.headers)
  for (const [k, v] of Object.entries(CORS_HEADERS)) headers.set(k, v)
  if (!headers.has('Content-Type')) headers.set('Content-Type', 'application/json')
  return new Response(res.body, { status: res.status, headers })
})

async function handle(req: Request): Promise<Response> {
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

  const nodeCode = body.node_code.toUpperCase()
  const wallet   = body.wallet_address.toLowerCase()

  const { data, error } = await supabase
    .from('nodes')
    .update({
      verification_tweet_url: body.tweet_url,
      verification_wallet_address: wallet,
      verification_submitted_at: new Date().toISOString(),
      verification_notified: false,   // AMI re-alerts the admin about this fresh submission
      // is_verified intentionally left untouched here -- a human reviews it next.
    })
    .eq('node_code', nodeCode)
    .select('node_code, is_verified')
    .single()

  if (error) {
    return new Response(JSON.stringify({ error: error.message }), { status: 500 })
  }

  // Best-effort email notification via Web3Forms (zero-config). The access key
  // is PUBLIC by design (these keys live in client-side HTML forms), so it's fine
  // hardcoded as a default — email then works with no setup at all. Emails land in
  // the inbox that created the key (turbousd2024@gmail.com). Override or disable
  // with the WEB3FORMS_ACCESS_KEY secret (set it empty to turn email off). We NEVER
  // fail the request just because email hiccuped — the submission already saved.
  const web3key = Deno.env.get('WEB3FORMS_ACCESS_KEY') ?? '44f243e1-3083-45f4-bb5e-5cddb84b0d7f'
  if (web3key) {
    try {
      await fetch('https://api.web3forms.com/submit', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', 'Accept': 'application/json' },
        body: JSON.stringify({
          access_key: web3key,
          subject: `New node verification: ${nodeCode}`,
          from_name: 'TurboUSD Node network',
          email: 'noreply@turbousd.com',   // reply-to; Web3Forms expects an email field
          node_code: nodeCode,
          tweet_url: body.tweet_url,
          wallet_address: wallet,
          message:
            `Node ${nodeCode} submitted a verification request.\n\n` +
            `X post: ${body.tweet_url}\n` +
            `Wallet: ${wallet}\n\n` +
            `Profile: https://network.turbousd.com/node/${nodeCode}`,
        }),
      })
    } catch (_err) { /* email is best-effort — the submission already succeeded */ }
  }

  return new Response(JSON.stringify({ node: data, message: 'Submitted for manual review' }), {
    status: 200,
    headers: { 'Content-Type': 'application/json' },
  })
}

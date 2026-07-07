// supabase/functions/mine-block/index.ts
//
// Triggered by a Supabase Cron Job once per block interval (e.g. every 60
// minutes -- see SIMULATED_BLOCK_MINUTES discussion in the firmware/UI; the
// real interval is whatever you configure here, independent of how the
// on-device countdown ring is dramatized).
//
// What it does:
//   1. Finds the currently "pending" block (mined_at IS NULL). If there is
//      none, creates the first one and exits (nothing to mine yet).
//   2. Finds the "active candidates": nodes with a heartbeat in the last
//      N minutes (i.e. genuinely online right now).
//   3. Picks a winner using a public, unpredictable randomness source (the
//      hash of a recent Base block) instead of Math.random() on the server,
//      so the choice cannot be steered by whoever controls this function.
//   4. Marks the block mined, credits the winner's reward balance, and
//      opens the next pending block.
//
// Deploy with: supabase functions deploy mine-block
// Schedule with: select cron.schedule('mine-block', '*/60 * * * *',
//   $$ select net.http_post(url:='https://<project>.functions.supabase.co/mine-block') $$);

import { createClient } from 'https://esm.sh/@supabase/supabase-js@2'

const supabaseUrl = Deno.env.get('SUPABASE_URL')!
const serviceRoleKey = Deno.env.get('SUPABASE_SERVICE_ROLE_KEY')!
const BASE_RPC_URL = Deno.env.get('BASE_RPC_URL') ?? 'https://mainnet.base.org'
const HEARTBEAT_WINDOW_MINUTES = 15 // must match the "is_online" window used elsewhere
const BLOCK_REWARD_TUSD = 100
const BLOCK_INTERVAL_MS = 60 * 60_000 // one block window; must match the on-device/web countdown

// Try the configured RPC first, then a couple of well-known public Base RPCs.
// A single flaky endpoint used to throw and 500 the whole function → the block
// never mined AND never reset (the reset only runs in the no-candidates path),
// so a node could be online for hours and the chain still froze at 00:00. With
// candidates present we now survive a total RPC outage (see the caller).
const BASE_RPCS = [
  BASE_RPC_URL,
  'https://base.publicnode.com',
  'https://base-rpc.publicnode.com',
  'https://1rpc.io/base',
]

async function fetchWithTimeout(url: string, ms: number): Promise<Response> {
  const ctrl = new AbortController()
  const t = setTimeout(() => ctrl.abort(), ms)
  try {
    return await fetch(url, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        jsonrpc: '2.0', id: 1, method: 'eth_getBlockByNumber', params: ['latest', false],
      }),
      signal: ctrl.signal,
    })
  } finally {
    clearTimeout(t)
  }
}

async function fetchLatestBaseBlockHash(): Promise<string> {
  // Public, free RPC call -- no API key needed. This is the "nobody can
  // predict or steer this" ingredient: nobody (including us) controls what
  // the next Base block hash will be ahead of time.
  //
  // Resilience: several endpoints, each with a hard timeout, and a few full
  // passes with a short backoff. A single slow/broken RPC can no longer stall
  // the whole miner — it keeps trying until one answers.
  const PASSES = 3
  const PER_REQUEST_TIMEOUT_MS = 4000
  let lastErr: unknown = null
  for (let pass = 0; pass < PASSES; pass++) {
    for (const url of BASE_RPCS) {
      try {
        const res = await fetchWithTimeout(url, PER_REQUEST_TIMEOUT_MS)
        const json = await res.json()
        const hash = json?.result?.hash as string | undefined
        if (hash && hash.startsWith('0x') && hash.length >= 10) return hash
        lastErr = new Error(`bad RPC response from ${url}`)
      } catch (e) {
        lastErr = e
      }
    }
    // brief backoff before the next full pass (skip after the last)
    if (pass < PASSES - 1) await new Promise(r => setTimeout(r, 500))
  }
  throw lastErr ?? new Error('all Base RPCs failed')
}

function pickWinnerIndex(randomnessHex: string, candidateCount: number): number {
  // Deterministic, auditable mapping from the public hash to an index.
  // Anyone can redo this calculation given the same hash and candidate list
  // to confirm the result wasn't tampered with after the fact.
  const hexDigits = randomnessHex.replace(/^0x/, '')
  const asBigInt = BigInt('0x' + hexDigits)
  return Number(asBigInt % BigInt(candidateCount))
}

Deno.serve(async (_req: Request) => {
  const supabase = createClient(supabaseUrl, serviceRoleKey)

  // 1. Find (or create) the pending block.
  //    We also select reward_tusd so the bot can override it via direct DB update
  //    and have that override carry through to the winner credit and the next block.
  const { data: pending, error: pendingError } = await supabase
    .from('mining_blocks')
    .select('id, block_number, reward_tusd, created_at')
    .is('mined_at', null)
    .maybeSingle()

  if (pendingError) {
    return new Response(JSON.stringify({ error: pendingError.message }), { status: 500 })
  }

  if (!pending) {
    const { data: lastBlock } = await supabase
      .from('mining_blocks')
      .select('block_number')
      .order('block_number', { ascending: false })
      .limit(1)
      .maybeSingle()
    const nextNumber = (lastBlock?.block_number ?? 0) + 1
    const { error: createError } = await supabase
      .from('mining_blocks')
      .insert({ block_number: nextNumber, reward_tusd: BLOCK_REWARD_TUSD, created_at: new Date().toISOString() })
    if (createError) {
      return new Response(JSON.stringify({ error: createError.message }), { status: 500 })
    }
    return new Response(JSON.stringify({ ok: true, action: 'created_first_pending_block', block_number: nextNumber }), { status: 200 })
  }

  // 2. Active candidates: nodes seen recently.
  const cutoff = new Date(Date.now() - HEARTBEAT_WINDOW_MINUTES * 60_000).toISOString()
  const { data: candidates, error: candidatesError } = await supabase
    .from('nodes')
    .select('id, is_verified')
    .eq('is_active', true)
    .gte('last_seen_at', cutoff)

  if (candidatesError) {
    return new Response(JSON.stringify({ error: candidatesError.message }), { status: 500 })
  }

  if (!candidates || candidates.length === 0) {
    // Nobody online right now — the block can't be mined. Previously we just
    // left it pending, but its created_at never advanced, so the countdown
    // (anchored on it) froze at 00:00 forever and mining appeared "stuck".
    // Fix: once this block's window has fully elapsed with no winner, RESTART
    // its countdown (created_at = now) so it visibly counts down again and
    // keeps retrying every interval until a node is finally online to mine it.
    const openedAt = pending.created_at ? new Date(pending.created_at).getTime() : 0
    // Null created_at is treated as overdue too (self-heals a block that
    // somehow opened without a timestamp — otherwise it could never reset).
    const overdue = openedAt === 0 || (Date.now() - openedAt) >= BLOCK_INTERVAL_MS
    if (overdue) {
      const { error: resetError } = await supabase
        .from('mining_blocks')
        .update({ created_at: new Date().toISOString() })
        .eq('id', pending.id)
      if (resetError) {
        return new Response(JSON.stringify({ error: resetError.message }), { status: 500 })
      }
      return new Response(JSON.stringify({
        ok: true, action: 'reset_pending_countdown', block_number: pending.block_number,
      }), { status: 200 })
    }
    // Window not elapsed yet — just wait for the next run.
    return new Response(JSON.stringify({ ok: true, action: 'no_active_candidates' }), { status: 200 })
  }

  // 3. Pick a winner using public on-chain randomness. If EVERY RPC is down we
  //    must not 500 (that would leave the block frozen forever, the exact stall
  //    we hit at #16): instead restart the countdown and retry next interval.
  let blockHash: string
  try {
    blockHash = await fetchLatestBaseBlockHash()
  } catch (_e) {
    await supabase
      .from('mining_blocks')
      .update({ created_at: new Date().toISOString() })
      .eq('id', pending.id)
    return new Response(JSON.stringify({
      ok: true, action: 'randomness_unavailable_reset', block_number: pending.block_number,
    }), { status: 200 })
  }
  const winnerIndex = pickWinnerIndex(blockHash, candidates.length)
  const winner = candidates[winnerIndex]

  // Unverified winners don't earn rewards (block is still mined and assigned,
  // but reward_tusd is 0 so the public feed shows it honestly).
  // Use the pending block's reward_tusd if set (allows bot override via /node reward),
  // falling back to the hardcoded constant if the column is null.
  const blockReward = pending.reward_tusd ?? BLOCK_REWARD_TUSD
  const actualReward = winner.is_verified ? blockReward : 0

  // 4. Confirm the block and credit the winner.
  const minedAt = new Date().toISOString()
  const { error: confirmError } = await supabase
    .from('mining_blocks')
    .update({
      winner_node_id:   winner.id,
      mined_at:         minedAt,
      randomness_source: blockHash,
      reward_tusd:      actualReward,
      candidates_count: candidates.length,
    })
    .eq('id', pending.id)

  if (confirmError) {
    return new Response(JSON.stringify({ error: confirmError.message }), { status: 500 })
  }

  // Track uptime windows for every candidate that was online this round.
  await supabase.rpc('increment_windows_online', {
    p_node_ids: candidates.map(c => c.id),
  })

  // Only credit reward balance if the winner is verified.
  if (actualReward > 0) {
    const { data: existingBalance } = await supabase
      .from('node_reward_balances')
      .select('total_tusd_earned, blocks_won')
      .eq('node_id', winner.id)
      .maybeSingle()

    await supabase.from('node_reward_balances').upsert({
      node_id: winner.id,
      total_tusd_earned: (existingBalance?.total_tusd_earned ?? 0) + actualReward,
      blocks_won: (existingBalance?.blocks_won ?? 0) + 1,
      updated_at: minedAt,
    })
  }

  // Open the next pending block.
  const { data: justMined } = await supabase
    .from('mining_blocks')
    .select('block_number')
    .eq('id', pending.id)
    .single()

  // Carry the reward forward to the next block so it persists until the bot
  // explicitly changes it again with /node reward.
  await supabase.from('mining_blocks').insert({
    block_number: (justMined?.block_number ?? pending.block_number) + 1,
    reward_tusd: blockReward,
    created_at: new Date().toISOString(),
  })

  return new Response(
    JSON.stringify({
      ok: true,
      action: 'block_mined',
      block_number: pending.block_number,
      winner_node_id: winner.id,
      winner_verified: winner.is_verified,
      reward_tusd: actualReward,
      randomness_source: blockHash,
    }),
    { status: 200, headers: { 'Content-Type': 'application/json' } }
  )
})

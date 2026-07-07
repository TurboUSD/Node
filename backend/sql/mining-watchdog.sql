-- mining-watchdog.sql  —  permanent self-healing for the block countdown.
--
-- WHAT IT DOES
-- A pg_cron job that runs every 5 minutes and, if the current PENDING block's
-- window has fully elapsed (created_at older than the 60-min interval), restarts
-- its countdown by setting created_at = now(). That means the ring can NEVER
-- stay frozen at 00:00 again: an unmined block just counts 60→0, restarts, and
-- keeps retrying forever until a node is online and the miner picks a winner.
--
-- WHY SQL (and why this does NOT replace the redeploy)
-- Picking the winner needs the on-chain Base RPC + hashing, which lives in the
-- Edge Function (mine-block). This watchdog only keeps the TIMESTAMP alive so
-- the chain never *looks* stuck; it deliberately does NOT call the miner (that
-- would mint blocks faster than once per interval). The RPC retries/fallbacks
-- that make the actual MINING resilient are in mine-block/index.ts and only take
-- effect after `supabase functions deploy mine-block`. So:
--   • Run this file  → guarantees no more frozen 00:00, no redeploy required.
--   • Redeploy too   → makes the mining step itself survive RPC outages on the
--                      first try instead of waiting for the next hourly run.
--
-- Safe to run repeatedly (idempotent): it re-schedules the same named job and
-- only touches created_at, never rewards or winners.

CREATE EXTENSION IF NOT EXISTS pg_cron;

-- Keep the interval in ONE place. Must match BLOCK_INTERVAL_MS in
-- mine-block/index.ts and the web/device countdown (60 minutes).
DO $$
BEGIN
  PERFORM cron.unschedule('mining-watchdog');
EXCEPTION WHEN OTHERS THEN
  NULL; -- wasn't scheduled yet
END $$;

SELECT cron.schedule(
  'mining-watchdog',
  '*/5 * * * *',
  $$
    UPDATE mining_blocks
    SET    created_at = now()
    WHERE  mined_at IS NULL
      AND  (created_at IS NULL OR created_at < now() - interval '60 minutes')
  $$
);

-- One immediate pass so a currently-stuck block restarts right now.
UPDATE mining_blocks
SET    created_at = now()
WHERE  mined_at IS NULL
  AND  (created_at IS NULL OR created_at < now() - interval '60 minutes');

-- Verify: should list BOTH jobs (mine-block hourly + mining-watchdog every 5m).
-- SELECT jobname, schedule, active FROM cron.job ORDER BY jobname;

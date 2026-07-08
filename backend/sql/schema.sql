-- ============================================================================
-- TurboUSD Node — canonical database schema (run ONCE on a fresh Supabase project)
-- ============================================================================
--
-- WHY THIS FILE EXISTS
-- The base tables were originally created in the Supabase dashboard, and the
-- other files in this folder are the incremental fixes/migrations that were
-- applied as bugs were found. That means there was no single "create the DB
-- from zero" file. This is that file: run it once on a NEW project and you get
-- the whole schema (tables, columns, views, policies, cron, helper functions)
-- without touching any of the dated migration files.
--
-- It is idempotent (CREATE ... IF NOT EXISTS / CREATE OR REPLACE / DROP VIEW IF
-- EXISTS), so it is safe to re-run. On an EXISTING deployment the individual
-- migration files still apply; this file supersedes them for fresh installs.
--
-- After running this:
--   1. Deploy the Edge Functions (see README "Running your own instance").
--   2. Fill in the cron section at the very bottom (project URL + anon key).
--   3. Point the web + firmware at this project's URL and anon key.
-- ============================================================================

-- ── Extensions ───────────────────────────────────────────────────────────────
create extension if not exists pgcrypto;   -- gen_random_uuid()
create extension if not exists pg_cron;     -- scheduled jobs
create extension if not exists pg_net;      -- net.http_post() from cron

-- ── Tables ───────────────────────────────────────────────────────────────────

-- Every registered device. Written only by the Edge Functions (service role);
-- readable by anon (RLS policy below) so the public setup/profile pages work.
create table if not exists nodes (
  id                   uuid primary key default gen_random_uuid(),
  mac_address          text unique,
  node_code            text unique not null,        -- 4 hex chars, from the MAC
  display_name         text,
  bio                  text,
  wallet_address       text,                         -- Base address for ₸USD payouts
  twitter_handle       text,
  country              text,
  city                 text,
  lat                  double precision,             -- precise (IP geo); blurred in the public view
  lng                  double precision,
  firmware_version     text default 'unknown',       -- ESP32 image, reported on heartbeat
  is_active            boolean default true,
  is_verified          boolean default false,
  is_genesis           boolean default false,
  created_at           timestamptz not null default now(),
  last_seen_at         timestamptz,
  -- device-reported uptime
  uptime_seconds       bigint,                        -- since last boot (resets on reboot)
  total_uptime_seconds bigint default 0,              -- cumulative across reboots (accumulated on heartbeat)
  -- display preferences
  temp_unit            text     default 'C',
  date_format          text     default 'DD/MM',
  time_format          text     default '24H',        -- '24H' | 'AMPM'
  week_start           smallint default 1,            -- 1 = Monday first, 0 = Sunday
  screen_brightness    smallint default 5,
  screen_always_on     boolean  default true,
  screen_timeout_mins  smallint default 10,
  screen_carousel      boolean  default false,
  screen_carousel_secs smallint default 10,
  -- alarm
  alarm_hour           smallint default 8,
  alarm_minute         smallint default 0,
  alarm_enabled        boolean  default false,
  alarm_volume         smallint default 2,            -- 1..5
  alarm_days           smallint default 127,          -- ISO bitmask, bit0=Mon; 127 = every day
  -- NFT gallery
  nft_wallet_address   text,
  nft_grid_size        smallint default 9,            -- 1 | 4 | 9
  nft_carousel_enabled boolean  default true,
  nft_slideshow_secs   smallint default 10,
  nft_pinlist          text,                          -- comma-joined "chain:contract:tokenId[:#bg]"
  nft_show_data        boolean  default true,
  nft_coll_order       text,                          -- comma-joined slugs, display order
  nft_coll_hidden      text,                          -- comma-joined hidden slugs
  nft_collections      jsonb,                         -- device-reported [{slug,name,floor}]
  -- screens + tickers
  screen_order         text,                          -- comma-joined ScreenId ints, pos 0 = Home
  screen_hidden        text,                          -- comma-joined hidden screen ids
  ticker_cols          smallint default 1             -- 1 or 2
);

-- Forward migrations for EXISTING databases (create-table-if-not-exists above
-- won't add columns to a table that already exists). Idempotent — safe to re-run.
alter table nodes add column if not exists screen_carousel      boolean  default false;
alter table nodes add column if not exists screen_carousel_secs smallint default 10;

-- Running reward + activity totals per node (upserted by mine-block).
create table if not exists node_reward_balances (
  node_id           uuid primary key references nodes(id) on delete cascade,
  total_tusd_earned numeric  default 0,
  blocks_won        integer  default 0,
  windows_online    integer  default 0,   -- 1-hour windows the node was online for
  tusd_paid         numeric  default 0,   -- cumulative paid out (pending = earned - paid)
  updated_at        timestamptz default now()
);

-- One row per mining block. A pending block has mined_at IS NULL.
create table if not exists mining_blocks (
  id                uuid primary key default gen_random_uuid(),
  block_number      integer unique not null,
  reward_tusd       numeric default 100,
  winner_node_id    uuid references nodes(id) on delete set null,
  mined_at          timestamptz,                       -- NULL while pending
  created_at        timestamptz not null default now(),-- opened-at; drives the countdown
  candidates_count  integer,
  randomness_source text                               -- Base block hash used to pick the winner
);

-- A node's screener tokens (one row per pinned ticker).
create table if not exists node_tickers (
  id            uuid primary key default gen_random_uuid(),
  node_id       uuid not null references nodes(id) on delete cascade,
  pool_address  text not null,
  chain_id      text not null,
  base_symbol   text not null,
  base_name     text,
  quote_symbol  text default 'USDC',
  display_order integer default 0,
  created_at    timestamptz not null default now(),
  unique (node_id, pool_address)
);

-- Per-device owner secret. RLS on with NO policies: anon can never read it,
-- only the Edge Functions (service role, which bypasses RLS) can.
create table if not exists node_setup_tokens (
  node_id    uuid primary key references nodes(id) on delete cascade,
  token      text not null,
  updated_at timestamptz not null default now()
);

-- Raw heartbeat log (optional diagnostics; not read by the UI).
create table if not exists node_heartbeats (
  id              bigserial primary key,
  node_id         uuid references nodes(id) on delete cascade,
  received_at     timestamptz not null default now(),
  uptime_seconds  bigint,
  wifi_rssi       integer,
  free_heap_bytes bigint
);

-- OTA release catalogue (one active row per target). latest-firmware reads this.
create table if not exists latest_firmware (
  id            bigserial primary key,
  target        text not null,            -- 'esp32s3' | 'rp2040'
  version       text not null,
  binary_url    text,
  sha256        text,
  release_notes text,
  is_active     boolean default true,
  published_at  timestamptz not null default now()
);

-- US national debt daily series (sync-debt-history upserts; debt-history reads).
create table if not exists us_debt_history (
  record_date    date primary key,
  total_debt_usd numeric not null
);

-- TUSD OHLCV candles (sync-ohlcv-history upserts; ohlcv-history reads).
create table if not exists tusd_ohlcv_history (
  candle_open_time timestamptz primary key,
  open_usd  numeric,
  high_usd  numeric,
  low_usd   numeric,
  close_usd numeric,
  volume_usd numeric
);

-- ── Helper stored functions (called via supabase.rpc from Edge Functions) ─────

-- Increment the online-window counter for every node that was online this round
-- (called by mine-block). Upserts a balances row if the node has none yet.
create or replace function increment_windows_online(p_node_ids uuid[])
returns void language sql as $$
  insert into node_reward_balances (node_id, windows_online)
  select unnest(p_node_ids), 1
  on conflict (node_id)
  do update set windows_online = node_reward_balances.windows_online + 1,
                updated_at = now();
$$;

-- NOTE: the reward-payout feature (rewards-payout / confirm-payout) and the
-- debt chart downsampling call three more RPCs: increment_tusd_paid,
-- coalesce_add_paid and debt_history_sampled. Those are secondary features; if
-- you use them, create the matching functions to fit your rewards-payout /
-- debt-history function signatures. A reasonable debt sampler:
create or replace function debt_history_sampled(p_limit integer default 60)
returns table (record_date date, total_debt_usd numeric) language sql as $$
  with ordered as (
    select record_date, total_debt_usd,
           row_number() over (order by record_date) as rn,
           count(*)      over () as total
    from us_debt_history
  )
  select record_date, total_debt_usd
  from ordered
  where rn % greatest(1, (total / greatest(1, p_limit))::int) = 0
     or rn = total
  order by record_date;
$$;

-- ── Public views (read-only, granted to anon) ─────────────────────────────────

-- Network map/list + node stats. lat/lng are snapped to a 3° grid (~300 km) so
-- the public map only ever shows a node's COUNTRY, never its precise location.
drop view if exists public_node_directory;
create view public_node_directory as
select
  n.node_code,
  n.display_name,
  n.bio,
  n.is_verified,
  n.is_genesis,
  (n.last_seen_at > now() - interval '10 minutes')            as is_online,
  coalesce(rb.total_tusd_earned, 0)                           as total_tusd_earned,
  coalesce(bw.blocks_won, 0)                                  as blocks_won,
  coalesce(rb.windows_online, 0)                              as windows_online,
  n.uptime_seconds                                            as uptime_seconds,
  coalesce(n.total_uptime_seconds, 0)                         as total_uptime_seconds,
  least(100, round(
      100.0 * coalesce(rb.windows_online, 0)
      / nullif((select count(*) from mining_blocks b where b.mined_at >= n.created_at), 0)
  ))::int                                                     as uptime_pct,
  n.created_at,
  n.last_seen_at,
  n.twitter_handle,
  n.country,
  n.city,
  (round(n.lat::numeric / 3) * 3)::float8                     as lat,
  (round(n.lng::numeric / 3) * 3)::float8                     as lng
from nodes n
left join node_reward_balances rb on rb.node_id = n.id
left join lateral (
  select count(*)::int as blocks_won
  from mining_blocks b
  where b.winner_node_id = n.id
) bw on true;

grant select on public_node_directory to anon, authenticated;

-- Block ticker / explorer feed. Includes the PENDING block (mined_at NULL) so
-- the web + device can run the countdown for the block being mined right now.
drop view if exists public_mining_feed;
create view public_mining_feed as
select
  b.block_number,
  b.reward_tusd,
  b.mined_at,
  b.created_at,
  b.candidates_count,
  b.randomness_source,
  w.display_name as winner_display_name,
  w.node_code    as winner_node_code,
  w.is_verified  as winner_is_verified,
  w.is_genesis   as winner_is_genesis,
  w.country      as winner_country
from mining_blocks b
left join nodes w on w.id = b.winner_node_id
order by b.block_number desc;

grant select on public_mining_feed to anon, authenticated;

-- A node's tickers, read by the device by node_code.
drop view if exists node_ticker_config;
create view node_ticker_config as
select
  n.node_code,
  t.pool_address,
  t.chain_id,
  t.base_symbol,
  t.base_name,
  t.quote_symbol,
  t.display_order
from node_tickers t
join nodes n on n.id = t.node_id
order by t.display_order;

grant select on node_ticker_config to anon, authenticated;

-- Nodes still owed ₸USD (used by rewards-payout, service role only — NOT granted
-- to anon so wallet addresses stay private).
drop view if exists pending_payouts;
create view pending_payouts as
select
  n.id            as node_id,
  n.node_code,
  n.wallet_address,
  (coalesce(rb.total_tusd_earned, 0) - coalesce(rb.tusd_paid, 0)) as pending_tusd
from nodes n
join node_reward_balances rb on rb.node_id = n.id
where n.wallet_address is not null
  and n.is_active
  and (coalesce(rb.total_tusd_earned, 0) - coalesce(rb.tusd_paid, 0)) > 0;

-- ── Row Level Security ────────────────────────────────────────────────────────
-- Public READ on nodes (the setup/profile pages read it with the anon key).
-- All WRITES go through Edge Functions using the service role, which bypasses
-- RLS, so opening read access does NOT let anyone modify data.
alter table nodes enable row level security;
grant select on nodes to anon, authenticated;
drop policy if exists nodes_anon_read on nodes;
create policy nodes_anon_read on nodes for select to anon, authenticated using (true);

-- Setup tokens: RLS on, no policies, revoke from anon (service role only).
alter table node_setup_tokens enable row level security;
revoke all on node_setup_tokens from anon, authenticated;

-- ── Scheduled jobs (pg_cron) ──────────────────────────────────────────────────
-- Replace <PROJECT-REF> and <ANON-KEY> below, then run this section. mine-block
-- opens/mines a block every hour; mining-watchdog restarts a pending block's
-- countdown if its window elapses unmined so the timer can never freeze at 00:00.
--
-- select cron.schedule('mine-block', '0 * * * *', $$
--   select net.http_post(
--     url     := 'https://<PROJECT-REF>.functions.supabase.co/mine-block',
--     headers := jsonb_build_object('Content-Type','application/json',
--                                   'Authorization','Bearer <ANON-KEY>'),
--     body    := '{}'::jsonb);
-- $$);
--
-- select cron.schedule('mining-watchdog', '*/5 * * * *', $$
--   update mining_blocks set created_at = now()
--   where mined_at is null
--     and (created_at is null or created_at < now() - interval '60 minutes');
-- $$);
--
-- Optional daily data syncs:
-- select cron.schedule('sync-debt-history',  '0 6 * * *', $$ select net.http_post(url:='https://<PROJECT-REF>.functions.supabase.co/sync-debt-history',  headers:=jsonb_build_object('Content-Type','application/json','Authorization','Bearer <ANON-KEY>'), body:='{}'::jsonb); $$);
-- select cron.schedule('sync-ohlcv-history', '0 7 * * *', $$ select net.http_post(url:='https://<PROJECT-REF>.functions.supabase.co/sync-ohlcv-history', headers:=jsonb_build_object('Content-Type','application/json','Authorization','Bearer <ANON-KEY>'), body:='{}'::jsonb); $$);

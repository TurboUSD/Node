// supabase/functions/sync-debt-history/index.ts
//
// Run once daily by a Supabase Cron Job. Pulls new rows from the Treasury
// Fiscal Data "Debt to the Penny" API and upserts them into
// us_debt_history. Devices never call Treasury directly for the chart's
// historical range -- they read this cached table instead (see
// debt-history Edge Function and debtHistorySampled() in api_client.h).
//
// First run needs a backfill: this function only fetches rows newer than
// whatever's already in the table, so for the very first run you'll want
// to either run it with a manually-set old `since` date, or call the
// Treasury API directly once with a wide date filter to seed the table.
//
// Deploy with: supabase functions deploy sync-debt-history
// Schedule with: select cron.schedule('sync-debt-history', '0 6 * * *',
//   $$ select net.http_post(url:='https://<project>.functions.supabase.co/sync-debt-history') $$);

import { createClient } from 'https://esm.sh/@supabase/supabase-js@2'

const supabaseUrl = Deno.env.get('SUPABASE_URL')!
const serviceRoleKey = Deno.env.get('SUPABASE_SERVICE_ROLE_KEY')!

const TREASURY_API_BASE = 'https://api.fiscaldata.treasury.gov/services/api/fiscal_service/v2/accounting/od/debt_to_penny'

interface TreasuryRow {
  record_date: string
  tot_pub_debt_out_amt: string
}

Deno.serve(async (_req: Request) => {
  const supabase = createClient(supabaseUrl, serviceRoleKey)

  const { data: latest } = await supabase
    .from('us_debt_history')
    .select('record_date')
    .order('record_date', { ascending: false })
    .limit(1)
    .maybeSingle()

  const sinceDate = latest?.record_date ?? '1990-01-01'

  let totalUpserted = 0
  let pageNumber = 1
  const pageSize = 1000

  while (true) {
    const url = `${TREASURY_API_BASE}?filter=record_date:gte:${sinceDate}&sort=record_date&page[size]=${pageSize}&page[number]=${pageNumber}`
    const res = await fetch(url)
    if (!res.ok) {
      return new Response(JSON.stringify({ error: `Treasury API returned ${res.status}` }), { status: 500 })
    }
    const json = await res.json()
    const rows: TreasuryRow[] = json.data ?? []
    if (rows.length === 0) break

    const upsertRows = rows.map((r) => ({
      record_date: r.record_date,
      total_debt_usd: parseFloat(r.tot_pub_debt_out_amt),
    }))

    const { error } = await supabase.from('us_debt_history').upsert(upsertRows, { onConflict: 'record_date' })
    if (error) {
      return new Response(JSON.stringify({ error: error.message }), { status: 500 })
    }

    totalUpserted += upsertRows.length
    if (rows.length < pageSize) break
    pageNumber++
  }

  return new Response(JSON.stringify({ ok: true, rows_upserted: totalUpserted, since: sinceDate }), {
    status: 200,
    headers: { 'Content-Type': 'application/json' },
  })
})

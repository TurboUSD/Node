// lib/supabase.ts — single shared Supabase client for the web app.
// Uses the anon key only: every write the public web pages need to make
// goes through an Edge Function (see backend/functions/), not directly
// against the database, so the anon key here only ever reads public views.

import { createClient } from '@supabase/supabase-js'

const supabaseUrl = process.env.NEXT_PUBLIC_SUPABASE_URL!
const supabaseAnonKey = process.env.NEXT_PUBLIC_SUPABASE_ANON_KEY!

export const supabase = createClient(supabaseUrl, supabaseAnonKey)

export const FUNCTIONS_BASE_URL = process.env.NEXT_PUBLIC_SUPABASE_FUNCTIONS_URL!

export async function callFunction<T>(name: string, body: unknown): Promise<T> {
  const res = await fetch(`${FUNCTIONS_BASE_URL}/${name}`, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      Authorization: `Bearer ${supabaseAnonKey}`,
    },
    body: JSON.stringify(body),
  })
  const json = await res.json()
  if (!res.ok) throw new Error(json.error ?? `Request to ${name} failed`)
  return json as T
}

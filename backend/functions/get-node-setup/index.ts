// supabase/functions/get-node-setup/index.ts
//
// Returns a node's full config for the OWNER setup page — requires the
// per-device setup token (the ?t=... embedded in the QR shown on the
// device's Settings popup). This replaces the setup page's direct anon REST
// select, which (a) let anyone read the config knowing only the public
// 4-char code, and (b) broke with "column ... does not exist" whenever the
// web's column list drifted ahead of the DB (the "No node found" bug).
//
// Body: { node_code, setup_token }
// 200 → { node: {...} }   403 → bad/missing token   404 → no such node
//
// Deploy: supabase functions deploy get-node-setup

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
  if (req.method !== 'POST') {
    return new Response(JSON.stringify({ error: 'Method not allowed' }), { status: 405, headers: CORS })
  }

  let body: { node_code?: string; setup_token?: string }
  try { body = await req.json() } catch {
    return new Response(JSON.stringify({ error: 'Invalid JSON' }), { status: 400, headers: CORS })
  }

  if (!body.node_code) {
    return new Response(JSON.stringify({ error: 'node_code is required' }), { status: 400, headers: CORS })
  }

  const supabase = createClient(supabaseUrl, serviceRoleKey)

  const { data: node, error } = await supabase
    .from('nodes')
    .select('*, node_setup_tokens ( token )')
    .eq('node_code', body.node_code.toUpperCase())
    .maybeSingle()

  if (error) {
    return new Response(JSON.stringify({ error: error.message }), { status: 500, headers: CORS })
  }
  if (!node) {
    return new Response(JSON.stringify({ error: 'Node not found' }), { status: 404, headers: CORS })
  }

  const rel = (node as { node_setup_tokens?: { token?: string } | { token?: string }[] }).node_setup_tokens
  const storedToken = Array.isArray(rel) ? rel[0]?.token : rel?.token

  // Pre-token firmware: no stored token yet → legacy code-only access until
  // the device heartbeats once on new firmware and a token gets stored.
  if (storedToken && storedToken !== body.setup_token) {
    return new Response(JSON.stringify({ error: 'invalid_token' }), { status: 403, headers: CORS })
  }

  // Never echo secrets back to the browser.
  delete (node as Record<string, unknown>).node_setup_tokens

  return new Response(JSON.stringify({ node }), { headers: CORS })
})

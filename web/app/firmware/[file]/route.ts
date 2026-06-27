// app/firmware/[file]/route.ts — same-origin proxy for firmware downloads.
//
// Why this exists: GitHub Release asset URLs
// (github.com/<repo>/releases/download/...) do NOT send an
// `Access-Control-Allow-Origin` header, so esp-web-tools' in-browser fetch from
// network.turbousd.com is blocked by CORS ("Failed to fetch"). This route runs
// server-side (no CORS applies to server fetches), pulls the asset from the
// latest GitHub Release, and re-serves it from our own origin with permissive
// CORS — so the browser flasher can download the firmware.
//
// The manifest's binary `path` points here (see firmware-esp32/manifest.json).

import { NextRequest } from 'next/server'

const REPO = 'turbousd/node'

// Allow-list of filenames we publish, so this can't be abused as an open proxy
// for arbitrary release assets.
const ALLOWED: Record<string, string> = {
  'firmware-esp32s3.bin': 'application/octet-stream',
  'manifest.json': 'application/json',
}

export const dynamic = 'force-dynamic' // never cache at the framework layer

export async function GET(
  _req: NextRequest,
  { params }: { params: { file: string } },
) {
  const file = params.file
  const contentType = ALLOWED[file]
  if (!contentType) {
    return new Response('Not found', { status: 404 })
  }

  const upstream = `https://github.com/${REPO}/releases/latest/download/${file}`
  let r: Response
  try {
    r = await fetch(upstream, { redirect: 'follow', cache: 'no-store' })
  } catch {
    return new Response('Upstream fetch failed', { status: 502 })
  }
  if (!r.ok || !r.body) {
    return new Response(`Upstream returned ${r.status}`, { status: 502 })
  }

  return new Response(r.body, {
    status: 200,
    headers: {
      'Content-Type': contentType,
      'Access-Control-Allow-Origin': '*', // robust even if served cross-origin
      'Cache-Control': 'no-store',
    },
  })
}

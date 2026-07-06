'use client'

// app/my-node/page.tsx — "My Node" entry point. The header link lands here.
// The visitor types their node code; we remember it on this device (localStorage)
// and open /node/<code>. Below the input, an explanation of how to get the
// owner-edit link (scan the QR from the device's Settings footer button).

import { useEffect, useState } from 'react'
import { useRouter } from 'next/navigation'
import SiteHeader from '@/components/SiteHeader'

const C = {
  bg: '#0a0a0a', card: '#111', border: '#262626',
  text: '#e8e8ea', muted: '#6e7280', green: '#43e397',
}

export default function MyNodePage() {
  const router = useRouter()
  const [saved, setSaved] = useState<string | null>(null)
  const [code, setCode] = useState('')

  useEffect(() => { setSaved(localStorage.getItem('turbousd_node_code')) }, [])

  function open(raw: string) {
    const norm = raw.trim().toUpperCase()
    if (!norm) return
    localStorage.setItem('turbousd_node_code', norm)
    router.push(`/node/${norm}`)
  }

  return (
    <main style={{ minHeight: '100vh', background: C.bg, color: C.text, fontFamily: 'system-ui, -apple-system, sans-serif' }}>
      <SiteHeader />
      <div style={{ maxWidth: 480, margin: '0 auto', padding: '32px 20px' }}>
        <h1 style={{ fontSize: 24, margin: '0 0 6px' }}>My Node</h1>
        <p style={{ color: C.muted, fontSize: 14, margin: '0 0 24px', lineHeight: 1.6 }}>
          Enter your node code to open its profile. We&apos;ll remember it on this device, so &quot;My Node&quot; always brings you straight back here.
        </p>

        {saved && (
          <div style={{
            background: C.card, border: `1px solid ${C.border}`, borderRadius: 12, padding: 16,
            marginBottom: 20, display: 'flex', alignItems: 'center', justifyContent: 'space-between', gap: 12,
          }}>
            <div style={{ minWidth: 0 }}>
              <div style={{ fontSize: 11, color: C.muted, textTransform: 'uppercase', letterSpacing: 0.8 }}>Saved on this device</div>
              <div style={{ fontSize: 18, fontWeight: 'bold' }}>{saved}</div>
            </div>
            <a href={`/node/${saved}`} style={{
              padding: '9px 16px', background: C.green, color: '#000', borderRadius: 8,
              fontWeight: 700, fontSize: 14, textDecoration: 'none', whiteSpace: 'nowrap',
            }}>View →</a>
          </div>
        )}

        <form onSubmit={e => { e.preventDefault(); open(code) }} style={{ display: 'flex', gap: 8, marginBottom: 8 }}>
          <input
            value={code}
            onChange={e => setCode(e.target.value)}
            placeholder="Node code (e.g. 9B17)"
            autoCapitalize="characters"
            autoCorrect="off"
            spellCheck={false}
            style={{
              flex: 1, minWidth: 0, background: C.card, border: `1px solid ${C.border}`, borderRadius: 8,
              padding: '11px 14px', color: C.text, fontSize: 15, outline: 'none', textTransform: 'uppercase',
            }}
          />
          <button type="submit" style={{
            padding: '11px 18px', background: C.green, color: '#000', border: 'none', borderRadius: 8,
            fontWeight: 700, fontSize: 14, cursor: 'pointer', whiteSpace: 'nowrap',
          }}>
            {saved ? 'Change' : 'Open'}
          </button>
        </form>
        <p style={{ color: C.muted, fontSize: 12, margin: '0 0 28px' }}>
          Your node code is the 4-character ID shown on the device and next to your node name across the network.
        </p>

        {/* Owner-edit (QR) explanation */}
        <div style={{ background: C.card, border: `1px solid ${C.border}`, borderRadius: 12, padding: '16px 18px' }}>
          <div style={{ fontSize: 14, fontWeight: 700, marginBottom: 8 }}>Want to edit your node&apos;s settings?</div>
          <p style={{ color: C.muted, fontSize: 13, lineHeight: 1.65, margin: 0 }}>
            Editing your node (name, bio, tickers, NFTs, alarm…) needs an <strong style={{ color: C.text }}>owner access token</strong> that only the device can hand out. On your device, open <strong style={{ color: C.text }}>Settings</strong> (tap the QR icon in the footer) and <strong style={{ color: C.text }}>scan the QR code</strong> — it opens the setup page with the correct token. You can also type the exact URL shown right below the QR.
          </p>
        </div>
      </div>
    </main>
  )
}

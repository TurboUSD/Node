'use client'

// app/setup/page.tsx — network.turbousd.com/setup

import { useState, useEffect } from 'react'
import { useRouter } from 'next/navigation'

const GITHUB_REPO = 'turbousd/node'

type LatestRelease = {
  version: string          // e.g. "0.1.0" (parsed from the fw-<ver>-<run> tag)
  publishedAt: string      // ISO date
  rp2040Url: string | null // direct .uf2 asset URL for this release
}

const C = {
  green:   '#43e397',
  onGreen: '#000000',
  bg:      '#000000',
  card:    '#0c0c0c',
  surface: '#141414',
  border:  '#1c1c1c',
  text:    '#e8e8e8',
  muted:   '#6e7280',
  yellow:  '#ffcf72',
  red:     '#ff6b6b',
}

export default function SetupPage() {
  const [nodeCode, setNodeCode] = useState('')
  const [latest, setLatest] = useState<LatestRelease | null>(null)
  const [latestError, setLatestError] = useState(false)
  const router = useRouter()

  // Pull the newest published release straight from GitHub so this page always
  // reflects what's actually downloadable — no version numbers baked into the
  // markup that silently go stale. Both the ESP32 (via esp-web-tools) and the
  // RP2040 .uf2 are served from the release's /latest/ URLs regardless, so this
  // is purely to *show* the version and link the correctly-named .uf2 asset.
  useEffect(() => {
    let cancelled = false
    fetch(`https://api.github.com/repos/${GITHUB_REPO}/releases/latest`, {
      headers: { Accept: 'application/vnd.github+json' },
    })
      .then((r) => (r.ok ? r.json() : Promise.reject(new Error(`HTTP ${r.status}`))))
      .then((data: { tag_name?: string; published_at?: string; assets?: { name: string; browser_download_url: string }[] }) => {
        if (cancelled) return
        // Release tags look like "fw-0.1.0-12" → show just the firmware version.
        const version = (data.tag_name ?? '').replace(/^fw-/, '').replace(/-\d+$/, '') || '—'
        const uf2 = data.assets?.find((a) => /^rp2040-.*\.uf2$/.test(a.name))
        setLatest({
          version,
          publishedAt: data.published_at ?? '',
          rp2040Url: uf2?.browser_download_url ?? null,
        })
      })
      .catch(() => { if (!cancelled) setLatestError(true) })
    return () => { cancelled = true }
  }, [])

  // Always-valid fallback URL (GitHub redirects /latest/download to the newest
  // release); used if the API call is rate-limited or fails.
  const rp2040DownloadUrl =
    latest?.rp2040Url ??
    `https://github.com/${GITHUB_REPO}/releases/latest/download/rp2040-${latest?.version ?? '0.1.0'}.uf2`

  function goToNode(e: React.FormEvent) {
    e.preventDefault()
    const code = nodeCode.trim().toUpperCase()
    if (code.length >= 4) router.push(`/setup/${code}`)
  }

  return (
    <div style={s.root}>
      {/* Header */}
      <header style={s.header}>
        <a href="/" style={s.back}>← Network</a>
        <span style={s.logoText}>TurboUSD Node Setup</span>
        <div style={{ width: 80 }} />
      </header>

      <div style={s.content}>

        {/* Hero */}
        <div style={s.hero}>
          <div style={s.heroBadge}>NEW NODE</div>
          <h1 style={s.heroTitle}>Get your node online</h1>
          <p style={s.heroSub}>
            Three steps: flash the firmware, connect to WiFi, personalize your node.
            Takes about 5 minutes. No coding required.
          </p>
        </div>

        {/* ── Step 1: Flash ── */}
        <StepCard number="1" title="Flash the firmware" accent={C.green}>
          <p style={s.stepBody}>
            Connect your SenseCAP D1 (the physical device) to your computer via USB-C, then click the
            button below. Your browser will ask you to select a device — pick the one that
            says "USB Serial" or "CP210x". The flash takes about 30 seconds.
          </p>
          <div style={s.browserNote}>
            <span style={{ color: C.yellow }}>⚠</span>{' '}
            Requires <strong style={{ color: C.text }}>Chrome or Edge</strong> on desktop.
            Firefox and Safari don't support WebSerial yet.
          </div>

          {/* Live "latest version" indicator so it's obvious what you're installing */}
          <div style={{ display: 'flex', alignItems: 'center', gap: 8, margin: '12px 0 4px', fontSize: 13, color: C.muted }}>
            <span>Latest firmware:</span>
            {latestError ? (
              <span>
                see{' '}
                <a href={`https://github.com/${GITHUB_REPO}/releases/latest`} target="_blank" rel="noreferrer" style={s.link}>
                  GitHub Releases
                </a>
              </span>
            ) : latest ? (
              <span style={{ color: C.green, fontWeight: 700 }}>
                v{latest.version}
                {latest.publishedAt && (
                  <span style={{ color: C.muted, fontWeight: 400 }}>
                    {' '}· released {new Date(latest.publishedAt).toLocaleDateString()}
                  </span>
                )}
              </span>
            ) : (
              <span style={{ opacity: 0.6 }}>checking…</span>
            )}
          </div>
          <p style={{ fontSize: 12, color: C.muted, opacity: 0.7, margin: '0 0 14px' }}>
            Both the Install button and the sensor-chip download below always fetch this latest version.
          </p>

          <div style={s.flashBox}>
            {/* Manifest comes from the latest Release (CI bumps its version
                field), not the static copy on main, so the version shown in the
                flasher dialog matches what's actually being installed. */}
            <esp-web-install-button
              manifest="https://github.com/turbousd/node/releases/latest/download/manifest.json"
            >
              <button slot="activate" style={s.flashBtn}>
                ⚡ Install TurboUSD Firmware
              </button>
              <span slot="unsupported" style={{ color: C.red, fontSize: 13 }}>
                Your browser doesn't support WebSerial. Please use Chrome or Edge on desktop.
              </span>
            </esp-web-install-button>
          </div>

          {/* Explains the flasher's built-in "Erase device?" prompt */}
          <div style={{ marginTop: 14, padding: '14px 16px', background: 'rgba(91,141,238,0.08)', borderRadius: 10, border: `1px solid ${C.border}` }}>
            <p style={{ fontSize: 13, fontWeight: 700, color: C.text, marginBottom: 6 }}>
              The flasher will ask: “Erase device? All data will be lost.”
            </p>
            <p style={{ fontSize: 13, color: C.muted, lineHeight: 1.55, marginBottom: 8 }}>
              That prompt comes from the flashing tool, not from us. Here&apos;s when to say yes:
            </p>
            <ul style={{ margin: 0, paddingLeft: 18, fontSize: 13, color: C.muted, lineHeight: 1.55 }}>
              <li>
                <strong style={{ color: C.text }}>First time on a brand-new device</strong> (still on the
                factory firmware): <strong style={{ color: C.text }}>erase</strong> — it clears the old
                factory data for a clean start.
              </li>
              <li style={{ marginTop: 4 }}>
                <strong style={{ color: C.text }}>Updating a device that already runs TurboUSD</strong>:{' '}
                <strong style={{ color: C.text }}>don&apos;t erase</strong>. Erasing wipes your saved WiFi,
                node identity and settings, so you&apos;d have to set the device up from scratch. A normal
                update keeps everything.
              </li>
            </ul>
          </div>

          <details style={s.details}>
            <summary style={s.detailsSummary}>Having trouble? Manual flash instructions</summary>
            <div style={{ paddingTop: 12 }}>
              <p style={{ color: C.muted, fontSize: 13, marginBottom: 8 }}>
                If the browser flasher doesn't work, you can flash manually with PlatformIO:
              </p>
              <ol style={s.ol}>
                <li>Install <a href="https://platformio.org" target="_blank" rel="noreferrer" style={s.link}>PlatformIO</a> (free, works with VS Code)</li>
                <li>Download the latest firmware from <a href="https://github.com/turbousd/node/releases" target="_blank" rel="noreferrer" style={s.link}>GitHub Releases</a></li>
                <li>Run: <code style={s.code}>pio run --target upload</code> from the <code style={s.code}>firmware-esp32/</code> folder</li>
              </ol>
              <p style={{ marginTop: 10, color: C.muted, fontSize: 12, opacity: 0.7 }}>
                "Port not found" error? Hold the BOOT button on the device while connecting the USB cable, then try again.
              </p>
            </div>
          </details>

          {/* ── RP2040 sub-chip flash ── */}
          <div style={{ marginTop: 20, padding: '16px', background: 'rgba(255,255,255,0.04)', borderRadius: 10, border: `1px solid ${C.border}` }}>
            <p style={{ fontSize: 13, fontWeight: 700, color: C.text, marginBottom: 6 }}>
              🔔 Also flash the sensor &amp; buzzer chip (RP2040) — one-time, takes 30 seconds
            </p>
            <p style={{ fontSize: 13, color: C.muted, marginBottom: 12, lineHeight: 1.5 }}>
              The SenseCAP D1 has a second chip (the RP2040) that drives the alarm buzzer
              <strong style={{ color: C.text }}> and reads the temperature &amp; humidity sensor</strong>.
              Until you flash it, the alarm stays silent and the temp/humidity on the Home screen
              show as <code style={s.code}>--</code>. It can&apos;t be flashed via browser — you
              need to drag a file onto it like a USB drive.
            </p>
            <ol style={{ ...s.ol, marginBottom: 12 }}>
              <li>
                <strong style={{ color: C.text }}>Enter bootloader mode:</strong>{' '}
                Hold the <strong style={{ color: C.yellow }}>BOOT button</strong> (small button on the top edge of the device),
                then connect the USB-C cable to your computer while keeping it held. Release after 2 seconds.
                A drive called <code style={s.code}>RPI-RP2</code> will appear on your computer.
              </li>
              <li>
                Download the file below and <strong style={{ color: C.text }}>drag it onto the RPI-RP2 drive</strong>.
                The drive will disappear and the device restarts automatically — that means it worked.
              </li>
            </ol>
            <a
              href={rp2040DownloadUrl}
              download
              style={{
                display: 'inline-flex', alignItems: 'center', gap: 8,
                padding: '9px 18px', borderRadius: 8, fontSize: 14, fontWeight: 600,
                background: C.yellow, color: '#000', textDecoration: 'none',
                cursor: 'pointer',
              }}
            >
              ⬇ Download {latest ? `rp2040-${latest.version}.uf2` : 'latest RP2040 firmware'}
            </a>
            <p style={{ marginTop: 10, fontSize: 12, color: C.muted, opacity: 0.7 }}>
              Can&apos;t see the BOOT button? It&apos;s the small button on the short top edge, next to the USB-C port.
              If the RPI-RP2 drive doesn&apos;t appear, try a different USB cable (some are charge-only).
            </p>
          </div>
        </StepCard>

        {/* ── Step 2: WiFi ── */}
        <StepCard number="2" title="Connect to WiFi" accent="#5b8dee">
          <p style={s.stepBody}>
            After flashing, the device restarts and shows:
            <strong style={{ color: C.text }}> "Connect your phone to: TurboUSD-Setup-XXXX"</strong>.
          </p>
          <div style={s.stepList}>
            <Step icon="📱" text="On your phone, open WiFi settings and connect to the TurboUSD-Setup network." />
            <Step icon="🌐" text="A setup page opens automatically (like a hotel captive portal). If not, go to 192.168.4.1 in your browser." />
            <Step icon="✅" text='Enter your home WiFi name and password, then tap "Save". The device connects and shows the main clock screen.' />
          </div>
          <div style={s.tipBox}>
            <strong style={{ color: '#5b8dee' }}>Tip:</strong>{' '}
            <span style={{ color: C.muted }}>The device connects to 2.4 GHz networks only. If your router broadcasts both bands,
            make sure 2.4 GHz is enabled or use its dedicated SSID.</span>
          </div>
        </StepCard>

        {/* ── Step 3: Configure ── */}
        <StepCard number="3" title="Set up your node" accent={C.yellow}>
          <p style={s.stepBody}>
            Once online, your node appears in the{' '}
            <a href="/network" style={s.link}>TurboUSD Network</a>{' '}
            with a temporary name. Scan the QR code on your device screen (on the footer) —
            it opens your personal setup page directly, no code needed.
          </p>
          <div style={s.stepList}>
            <Step icon="✏️" text="Set a name and bio — shown on the public network page." />
            <Step icon="💳" text="Add your wallet address (Base network) — required to receive ₸USD mining rewards." />
            <Step icon="🌍" text="Set your country and optional city." />
            <Step icon="🏆" text="Submit for verification: post a video on X tagging @turbousd and showing your node's home screen — it must match your node ID — to earn the ✓ badge and start receiving rewards." />
          </div>
        </StepCard>

        {/* Returning user shortcut — below step 3 */}
        <div style={s.returnCard}>
          <div style={s.returnLabel}>Already flashed and connected your node to WiFi?</div>
          <p style={s.returnDesc}>
            Scan the QR code on your device to go directly to your setup page.
            Or enter your node code manually:
          </p>
          <form onSubmit={goToNode} style={{ display: 'flex', gap: 8 }}>
            <input
              style={s.codeInput}
              value={nodeCode}
              onChange={e => setNodeCode(e.target.value.toUpperCase())}
              placeholder="Node code (e.g. A3F2)"
              maxLength={6}
            />
            <button type="submit" style={s.codeBtn}>Open →</button>
          </form>
          <p style={s.returnHint}>Your node code is shown on the device's home screen.</p>
        </div>

        {/* Footer */}
        <p style={s.footer}>
          Questions? Find us on{' '}
          <a href="https://x.com/turbousd" target="_blank" rel="noreferrer" style={s.link}>X (@TurboUSD)</a>
          {' '}| Software is{' '}
          <a href="https://github.com/turbousd/node" target="_blank" rel="noreferrer" style={s.link}>open source</a>.
        </p>
      </div>

      <script type="module" src="https://unpkg.com/esp-web-tools@10/dist/web/install-button.js?module" />

      <style>{`
        esp-web-install-button { display: block; }
        details > summary { cursor: pointer; }
        input:focus, select:focus, textarea:focus { border-color: ${C.green} !important; outline: none; }
      `}</style>
    </div>
  )
}

function StepCard({ number, title, accent, children }: {
  number: string; title: string; accent: string; children: React.ReactNode
}) {
  return (
    <div style={{ ...s.card, borderLeftColor: accent }}>
      <div style={s.stepHeader}>
        <div style={{ ...s.stepNum, color: accent, borderColor: accent }}>{number}</div>
        <h2 style={s.stepTitle}>{title}</h2>
      </div>
      {children}
    </div>
  )
}

function Step({ icon, text }: { icon: string; text: string }) {
  return (
    <div style={{ display: 'flex', gap: 12, alignItems: 'flex-start' }}>
      <span style={{ fontSize: 18, flexShrink: 0, marginTop: 1 }}>{icon}</span>
      <span style={{ fontSize: 13, color: C.muted, lineHeight: 1.6 }}>{text}</span>
    </div>
  )
}

const s: Record<string, React.CSSProperties> = {
  root:    { minHeight: '100vh', background: C.bg, color: C.text, fontFamily: 'system-ui, -apple-system, sans-serif' },
  content: { maxWidth: 680, margin: '0 auto', padding: '32px 20px 80px' },

  header: {
    display: 'flex', alignItems: 'center', justifyContent: 'space-between',
    padding: '0 20px', height: 56, borderBottom: `1px solid ${C.border}`,
    position: 'sticky', top: 0, background: 'rgba(0,0,0,0.92)',
    backdropFilter: 'blur(12px)', zIndex: 10,
  },
  back:     { color: C.muted, textDecoration: 'none', fontSize: 14 },
  logoWrap: { display: 'flex', alignItems: 'center', gap: 10 },
  logoText: { fontSize: 16, fontWeight: 'bold', color: C.text },

  hero:      { textAlign: 'center', marginBottom: 36 },
  heroBadge: { display: 'inline-block', background: `${C.green}18`, border: `1px solid ${C.green}40`, color: C.green, fontSize: 10, fontWeight: 'bold', letterSpacing: 2, padding: '4px 10px', borderRadius: 4, marginBottom: 14 },
  heroTitle: { fontSize: 28, fontWeight: 'bold', marginBottom: 12, letterSpacing: -0.5 },
  heroSub:   { fontSize: 15, color: C.muted, lineHeight: 1.65, maxWidth: 460, margin: '0 auto' },

  returnCard:  { background: C.card, border: `1px solid ${C.border}`, borderRadius: 12, padding: '18px 20px', marginBottom: 32, marginTop: 16 },
  returnLabel: { fontSize: 11, color: C.muted, marginBottom: 8, textTransform: 'uppercase', letterSpacing: 1, fontWeight: 600 },
  returnDesc:  { fontSize: 13, color: '#9aa0b0', marginBottom: 12, marginTop: 0, lineHeight: 1.5 },
  codeInput: {
    flex: 1, padding: '10px 14px', background: C.surface, color: C.text,
    border: '1px solid #3a3a3a', borderRadius: 8, fontSize: 16,
    fontFamily: 'monospace', textTransform: 'uppercase', letterSpacing: 4,
  },
  codeBtn:    { padding: '10px 20px', background: C.surface, color: C.text, border: '1px solid #3a3a3a', borderRadius: 8, fontWeight: 'bold', fontSize: 14, cursor: 'pointer' },
  returnHint: { fontSize: 12, color: '#9aa0b0', marginTop: 8, marginBottom: 0 },

  card: {
    background: C.card, border: `1px solid ${C.border}`,
    borderLeft: `3px solid ${C.green}`, borderRadius: 12,
    padding: '24px 24px 20px', marginBottom: 16,
  },
  stepHeader: { display: 'flex', alignItems: 'center', gap: 14, marginBottom: 14 },
  stepNum:    { width: 30, height: 30, borderRadius: '50%', border: '2px solid', display: 'flex', alignItems: 'center', justifyContent: 'center', fontSize: 15, fontWeight: 'bold', flexShrink: 0 },
  stepTitle:  { fontSize: 17, fontWeight: 'bold', margin: 0 },
  stepBody:   { color: C.muted, fontSize: 14, lineHeight: 1.7, margin: '0 0 16px' },
  stepList:   { display: 'flex', flexDirection: 'column', gap: 12, marginBottom: 14 },

  browserNote: { background: `${C.yellow}10`, border: `1px solid ${C.yellow}30`, borderRadius: 8, padding: '10px 14px', fontSize: 13, color: C.muted, marginBottom: 14 },

  flashBox: { marginBottom: 14 },
  flashBtn: {
    width: '100%', padding: '14px 20px',
    background: 'linear-gradient(135deg, #0a2218, #1a5e38)',
    color: C.text, border: `1px solid ${C.green}`, borderRadius: 10,
    fontWeight: 'bold', fontSize: 16, cursor: 'pointer', letterSpacing: 0.5,
  },

  details:        { borderTop: `1px solid ${C.border}`, paddingTop: 14 },
  detailsSummary: { fontSize: 12, color: C.muted, opacity: 0.7 },
  ol:             { paddingLeft: 20, color: C.muted, fontSize: 13, lineHeight: 2 },

  tipBox: { background: '#5b8dee10', border: '1px solid #5b8dee30', borderRadius: 8, padding: '10px 14px', fontSize: 13, marginTop: 10 },

  link:   { color: C.green, textDecoration: 'none' },
  code:   { background: C.surface, border: `1px solid ${C.border}`, borderRadius: 4, padding: '2px 6px', fontFamily: 'monospace', fontSize: 12, color: C.muted },
  footer: { textAlign: 'center', fontSize: 13, color: '#9aa0b0', marginTop: 36 },
}

declare global {
  namespace JSX {
    interface IntrinsicElements {
      'esp-web-install-button': React.DetailedHTMLProps<React.HTMLAttributes<HTMLElement>, HTMLElement> & {
        manifest?: string
      }
    }
  }
}

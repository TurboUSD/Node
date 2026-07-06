// components/SiteFooter.tsx — shared footer rendered on EVERY page (mounted
// in app/layout.tsx): link back to turbousd.com plus the same social set as
// the turbousd.com header (X, Telegram, Dexscreener, Uniswap).

const BORDER  = '#1c1c1c'
const SURFACE = '#141414'
const TEXT    = '#e8e8e8'

function SocialIcon({ href, label, children }: { href: string; label: string; children: React.ReactNode }) {
  return (
    <a href={href} target="_blank" rel="noreferrer" aria-label={label} title={label}
      style={{
        width: 32, height: 32, borderRadius: '50%', background: SURFACE,
        border: `1px solid ${BORDER}`, color: TEXT,
        display: 'flex', alignItems: 'center', justifyContent: 'center',
      }}>
      {children}
    </a>
  )
}

export default function SiteFooter() {
  return (
    <footer style={{
      background: '#000', borderTop: `1px solid ${BORDER}`,
      padding: '26px 20px calc(34px + env(safe-area-inset-bottom, 0px))',
      display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 26, flexWrap: 'wrap',
      fontFamily: 'system-ui, -apple-system, sans-serif',
    }}>
      <a href="https://turbousd.com" target="_blank" rel="noreferrer"
        style={{ display: 'flex', alignItems: 'center', gap: 8, textDecoration: 'none' }}>
        {/* eslint-disable-next-line @next/next/no-img-element */}
        <img src="https://turbousd.com/wp-content/uploads/2025/07/TurboUSD_t.png" alt="" style={{ height: 26 }} />
        <span style={{ color: TEXT, fontSize: 13, fontWeight: 700 }}>turbousd.com</span>
      </a>
      <div style={{ display: 'flex', alignItems: 'center', gap: 16 }}>
        <SocialIcon href="https://x.com/turbousd" label="X">
          <svg viewBox="0 0 24 24" width="16" height="16" fill="currentColor"><path d="M18.244 2.25h3.308l-7.227 8.26 8.502 11.24H16.17l-5.214-6.817L4.99 21.75H1.68l7.73-8.835L1.254 2.25H8.08l4.713 6.231zm-1.161 17.52h1.833L7.084 4.126H5.117z"/></svg>
        </SocialIcon>
        <SocialIcon href="https://t.me/turbo_usd" label="Telegram">
          <svg viewBox="0 0 24 24" width="16" height="16" fill="currentColor"><path d="M11.944 0A12 12 0 0 0 0 12a12 12 0 0 0 12 12 12 12 0 0 0 12-12A12 12 0 0 0 12 0zm4.962 7.224c.1-.002.321.023.465.14a.5.5 0 0 1 .171.325c.016.093.036.306.02.472-.18 1.898-.962 6.502-1.36 8.627-.168.9-.499 1.201-.82 1.23-.696.065-1.225-.46-1.9-.902-1.056-.693-1.653-1.124-2.678-1.8-1.185-.78-.417-1.21.258-1.91.177-.184 3.247-2.977 3.307-3.23.007-.032.014-.15-.056-.212s-.174-.041-.249-.024c-.106.024-1.793 1.14-5.061 3.345-.48.33-.913.49-1.302.48-.428-.008-1.252-.241-1.865-.44-.752-.245-1.349-.374-1.297-.789.027-.216.325-.437.893-.663 3.498-1.524 5.83-2.529 6.998-3.014 3.332-1.386 4.025-1.627 4.476-1.635z"/></svg>
        </SocialIcon>
        <SocialIcon href="https://dexscreener.com/base/0x3d5e487B21E0569048c4D1A60E98C36e1B09DB07" label="Dexscreener">
          {/* eslint-disable-next-line @next/next/no-img-element */}
          <img src="https://dexscreener.com/favicon.png" alt="" width={16} height={16} style={{ display: 'block' }} />
        </SocialIcon>
        <SocialIcon href="https://app.uniswap.org/swap?outputCurrency=0x3d5e487B21E0569048c4D1A60E98C36e1B09DB07&chain=base" label="Uniswap">
          {/* eslint-disable-next-line @next/next/no-img-element */}
          <img src="https://app.uniswap.org/favicon.png" alt="" width={16} height={16} style={{ display: 'block' }} />
        </SocialIcon>
      </div>
    </footer>
  )
}

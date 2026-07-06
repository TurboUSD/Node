// components/SiteFooter.tsx — shared footer rendered on EVERY page (mounted in
// app/layout.tsx). Mirrors the treasury footer: an "Open source on GitHub" link
// to the Node repo, then a line with turbousd.com · ₸USD Network · social icons.
// Icons are monochrome SVGs (currentColor) so none of them show a white
// favicon background.

import { SOCIAL_LINKS, IconGitHub } from './SocialIcons'

const GOLD     = '#43e397'
const TEXT_DIM = '#888888'
const BORDER   = '#1c1c1c'
const GITHUB_URL = 'https://github.com/TurboUSD/Node'

export default function SiteFooter() {
  return (
    <footer style={{
      background: '#000', borderTop: `1px solid ${BORDER}`,
      padding: '28px 20px calc(30px + env(safe-area-inset-bottom, 0px))',
      textAlign: 'center', fontFamily: 'system-ui, -apple-system, sans-serif',
      fontSize: 14, color: TEXT_DIM,
    }}>
      <p style={{ margin: '0 0 8px' }}>
        <a href={GITHUB_URL} target="_blank" rel="noopener noreferrer"
          style={{ color: TEXT_DIM, textDecoration: 'none' }}>
          <span style={{ display: 'inline-flex', alignItems: 'center', gap: 6 }}>
            <IconGitHub size={16} />
            Open source on GitHub
          </span>
        </a>
      </p>
      <p style={{ margin: 0, display: 'inline-flex', alignItems: 'center', gap: 8, flexWrap: 'wrap', justifyContent: 'center' }}>
        <a href="https://turbousd.com" target="_blank" rel="noopener noreferrer"
          style={{ color: GOLD, textDecoration: 'none' }}>
          turbousd.com
        </a>
        <span>{'· ₸USD Network ·'}</span>
        <span style={{ display: 'inline-flex', alignItems: 'center', gap: 14 }}>
          {SOCIAL_LINKS.map(({ label, href, icon }) => (
            <a key={label} href={href} target="_blank" rel="noopener noreferrer" aria-label={label}
              style={{ color: TEXT_DIM, display: 'flex' }}>
              {icon}
            </a>
          ))}
        </span>
      </p>
    </footer>
  )
}

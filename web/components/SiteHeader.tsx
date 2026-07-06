'use client'

// components/SiteHeader.tsx — shared header for network.turbousd.com, mirroring
// the turbousd.com / treasury header. DESKTOP (>=1024px): logo + "₸USD Network"
// with the nav links inline next to the logo on the left, and the social icons
// on the right — no burger. MOBILE: a burger menu holds the links, the social
// icons and a "Get ₸USD" button.

import { useEffect, useRef, useState } from 'react'
import Link from 'next/link'
import { usePathname } from 'next/navigation'
import { SOCIAL_LINKS, BUY_URL } from './SocialIcons'

const GREEN  = '#43e397'
const BORDER = '#1c1c1c'
const MUTED  = '#888'
const DESKTOP_MIN = 1024

function BurgerIcon() {
  return (
    <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke={MUTED} strokeWidth="2" strokeLinecap="round">
      <path d="M3 6h18M3 12h18M3 18h18" />
    </svg>
  )
}

function CloseIcon() {
  return (
    <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke={MUTED} strokeWidth="2" strokeLinecap="round">
      <path d="M6 6l12 12M18 6L6 18" />
    </svg>
  )
}

export default function SiteHeader() {
  const pathname = usePathname()
  const [open, setOpen] = useState(false)
  const [savedNodeCode, setSavedNodeCode] = useState<string | null>(null)
  const [isDesktop, setIsDesktop] = useState(false)
  const menuRef = useRef<HTMLDivElement>(null)

  useEffect(() => {
    setSavedNodeCode(localStorage.getItem('turbousd_node_code'))
    const update = () => setIsDesktop(window.innerWidth >= DESKTOP_MIN)
    update()
    window.addEventListener('resize', update)
    return () => window.removeEventListener('resize', update)
  }, [])

  useEffect(() => { setOpen(false) }, [pathname])
  useEffect(() => {
    if (!open) return
    const handler = (e: MouseEvent) => {
      if (menuRef.current?.contains(e.target as Node)) return
      setOpen(false)
    }
    document.addEventListener('mousedown', handler)
    return () => document.removeEventListener('mousedown', handler)
  }, [open])

  const links: { label: string; href: string; external?: boolean }[] = [
    { label: 'Home', href: 'https://turbousd.com', external: true },
    { label: 'Live network', href: '/' },
    { label: 'The Device', href: '/node' },
    savedNodeCode
      ? { label: 'My Node', href: `/node/${savedNodeCode}` }
      : { label: 'Setup', href: '/setup' },
  ]

  const Socials = ({ color, gap }: { color: string; gap: number }) => (
    <div style={{ display: 'flex', alignItems: 'center', gap }}>
      {SOCIAL_LINKS.map(({ label, href, icon }) => (
        <a key={label} href={href} target="_blank" rel="noopener noreferrer" aria-label={label}
          style={{ color, display: 'flex' }}>
          {icon}
        </a>
      ))}
    </div>
  )

  return (
    <div ref={menuRef} style={{
      position: 'sticky', top: 0, zIndex: 1000, width: '100%',
      borderBottom: `1px solid ${BORDER}`, background: 'rgba(0,0,0,0.92)', backdropFilter: 'blur(12px)',
    }}>
      <div style={{
        maxWidth: 1100, margin: '0 auto', padding: '0 20px', height: 56,
        display: 'flex', alignItems: 'center', justifyContent: 'space-between',
      }}>
        {/* Left: logo + name, with inline links right next to it on desktop */}
        <div style={{ display: 'flex', alignItems: 'center', gap: 22, minWidth: 0 }}>
          <Link href="/" style={{ display: 'flex', alignItems: 'center', gap: 10, textDecoration: 'none', color: '#fff' }}>
            {/* eslint-disable-next-line @next/next/no-img-element */}
            <img
              src="https://turbousd.com/wp-content/uploads/2025/07/TurboUSD_t.png"
              alt="₸USD" style={{ height: 36, width: 'auto', objectFit: 'contain', display: 'block' }}
            />
            <span style={{ fontSize: 18, fontWeight: 'bold', letterSpacing: -0.5, color: '#fff', lineHeight: 1, whiteSpace: 'nowrap' }}>
              {'₸USD Network'}
            </span>
          </Link>

          {isDesktop && (
            <nav style={{ display: 'flex', alignItems: 'center', gap: 4 }}>
              {links.map(({ label, href, external }) => {
                const isActive = !external && pathname === href
                const style: React.CSSProperties = {
                  padding: '6px 10px', fontSize: 14, fontWeight: 500, whiteSpace: 'nowrap',
                  color: isActive ? GREEN : MUTED, textDecoration: 'none',
                }
                return external ? (
                  <a key={href} href={href} target="_blank" rel="noopener noreferrer" style={style}>{label}</a>
                ) : (
                  <Link key={href} href={href} style={style}>{label}</Link>
                )
              })}
            </nav>
          )}
        </div>

        {/* Right: social icons on desktop, burger on mobile */}
        {isDesktop ? (
          <Socials color="#a6a6a6" gap={14} />
        ) : (
          <button
            onClick={() => setOpen(o => !o)}
            aria-label={open ? 'Close menu' : 'Open menu'}
            style={{ background: 'none', border: 'none', cursor: 'pointer', padding: 4, display: 'flex', alignItems: 'center' }}
          >
            {open ? <CloseIcon /> : <BurgerIcon />}
          </button>
        )}
      </div>

      {/* Mobile dropdown menu */}
      {!isDesktop && (
        <div style={{
          position: 'absolute', left: 0, right: 0, top: 56, overflow: 'hidden',
          background: '#000', zIndex: 50,
          maxHeight: open ? 420 : 0, transition: 'max-height 0.3s ease',
          boxShadow: open ? '0 8px 24px rgba(0,0,0,0.6)' : 'none',
        }}>
          <div style={{ padding: '10px 24px 16px' }}>
            {links.map(({ label, href, external }) => {
              const isActive = !external && pathname === href
              const style: React.CSSProperties = {
                display: 'block', padding: '7px 0', textAlign: 'center', fontSize: 15, fontWeight: 500,
                color: isActive ? GREEN : '#fff', textDecoration: 'none',
              }
              return external ? (
                <a key={href} href={href} target="_blank" rel="noopener noreferrer" style={style} onClick={() => setOpen(false)}>{label}</a>
              ) : (
                <Link key={href} href={href} style={style} onClick={() => setOpen(false)}>{label}</Link>
              )
            })}

            <div style={{ display: 'flex', justifyContent: 'center', padding: '14px 0' }}>
              <Socials color="#fff" gap={20} />
            </div>

            <div style={{ display: 'flex', justifyContent: 'center', paddingBottom: 4 }}>
              <a href={BUY_URL} target="_blank" rel="noopener noreferrer"
                style={{
                  display: 'inline-block', textAlign: 'center', padding: '9px 40px', fontSize: 14, fontWeight: 600,
                  borderRadius: 24, border: `1px solid ${GREEN}`, color: GREEN, background: 'transparent',
                  textDecoration: 'none',
                }}
                onClick={() => setOpen(false)}
              >
                {'Get ₸USD'}
              </a>
            </div>
          </div>
        </div>
      )}
    </div>
  )
}

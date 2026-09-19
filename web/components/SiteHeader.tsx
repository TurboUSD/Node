'use client'

// components/SiteHeader.tsx — the turbousd.com header (menu, live ticker, AMI eye and treasury donut,
// Get ₸USD), shared with treasury.turbousd.com and store.turbousd.com. The bar underneath carries this
// site's own sections. Markup and styles live in components/tusd-chrome.

import Link from 'next/link'
import { usePathname } from 'next/navigation'
import { TusdHeader } from './tusd-chrome'

const TABS = [
  { label: 'Live network', href: '/' },
  { label: 'The Device', href: '/node' },
  { label: 'My Node', href: '/my-node' },
]

export default function SiteHeader() {
  const pathname = usePathname() ?? '/'
  // /node is the device page; /node/<code> is someone's own node, which lives under "My Node"
  const isActive = (href: string) =>
    href === '/'
      ? pathname === '/'
      : href === '/node'
        ? pathname === '/node'
        : pathname === '/my-node' || (pathname.startsWith('/node/') && pathname.length > 6)

  return (
    <TusdHeader
      site={{ label: '₸USD Network', href: '/' }}
      tabs={TABS.map((t) => ({ ...t, active: isActive(t.href) }))}
      Link={Link}
    />
  )
}

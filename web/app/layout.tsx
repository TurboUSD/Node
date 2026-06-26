import type { Metadata } from 'next'

export const metadata: Metadata = {
  title: 'TurboUSD Network',
  description: 'TurboUSD node network — mining dashboard, live stats, and node setup.',
}

export default function RootLayout({ children }: { children: React.ReactNode }) {
  return (
    <html lang="en">
      <head>
        {/* Viewport — disable user-scaling so it feels like a native app */}
        <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, viewport-fit=cover" />

        {/* PWA manifest + theme */}
        <link rel="manifest" href="/manifest.json" />
        <meta name="theme-color" content="#000000" />

        {/* iOS PWA: full-screen standalone experience */}
        <meta name="apple-mobile-web-app-capable" content="yes" />
        <meta name="apple-mobile-web-app-status-bar-style" content="black-translucent" />
        <meta name="apple-mobile-web-app-title" content="₸USD" />

        {/* Android PWA */}
        <meta name="mobile-web-app-capable" content="yes" />

        {/* Icons */}
        <link rel="icon" href="https://turbousd.com/wp-content/uploads/2026/04/cropped-TurboUSD_tc-32x32.png" sizes="32x32" />
        <link rel="icon" href="https://turbousd.com/wp-content/uploads/2026/04/cropped-TurboUSD_tc-192x192.png" sizes="192x192" />
        <link rel="apple-touch-icon" href="https://turbousd.com/wp-content/uploads/2026/04/cropped-TurboUSD_tc-180x180.png" />
      </head>
      <body style={{ margin: 0 }}>{children}</body>
    </html>
  )
}

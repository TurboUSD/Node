'use client'

// components/NodeBadges.tsx — verified / unverified / genesis badges with the
// same dark info-modal behaviour used on the network dashboard: hover shows a
// native tooltip (desktop), click/tap opens a centered explanation modal.

import { useState } from 'react'

const BORDER = '#262626'
const YELLOW = '#ffcf72'
const BLUE   = '#5b8dee'
const MUTED  = '#6e7280'
const TEXT   = '#e8e8ea'

const VERIFY_HELP =
  'To get verified:\n' +
  '1. Post a video on X showing this node running, tagging @turbousd\n' +
  '2. Write your node name on paper, show it matches your screen\n' +
  '3. Include the wallet holding your ₸USD\n' +
  '4. We manually review and whitelist your node'

const VERIFIED_HELP =
  'This node has been manually verified by the TurboUSD team: its owner proved ' +
  'the hardware is real and running. Verified nodes appear with the blue check ' +
  'across the network.'

const GENESIS_HELP =
  'One of the founding nodes that joined the TurboUSD network at launch. ' +
  'The lightning badge is permanent. It marks the earliest supporters of the network.'

const LOCATION_HELP =
  'This location is approximate, anonymized to roughly country level (about 300 km).\n\n' +
  'The network NEVER stores a node’s real position: the location is derived from the ' +
  'node’s IP address and snapped to a coarse grid before it ever reaches the database. ' +
  'It is never editable, so no exact address can be entered.'

// Small circled "i" info icon shown next to a node's location. Tap/hover explains
// that the location is anonymized and never stored precisely (see LOCATION_HELP).
export function LocationNote({ color = MUTED }: { color?: string }) {
  const [open, setOpen] = useState(false)
  return (
    <>
      <span
        title="Approximate location (anonymized). Tap to learn more"
        onClick={e => { e.stopPropagation(); e.preventDefault(); setOpen(true) }}
        style={{
          display: 'inline-flex', alignItems: 'center', justifyContent: 'center',
          cursor: 'help', marginLeft: 3, userSelect: 'none',
          verticalAlign: 'super',   // sit as a superscript above the country text
        }}
      >
        {/* Crisp SVG info glyph — the old CSS italic "i" rendered off-centre and
            looked crooked in the tiny circle. Drawn here so it's always centred. */}
        <svg width={11} height={11} viewBox="0 0 16 16" aria-hidden="true" style={{ display: 'block' }}>
          <circle cx="8" cy="8" r="7" fill="none" stroke={color} strokeWidth="1.4" />
          <circle cx="8" cy="4.7" r="1.05" fill={color} />
          <rect x="7.05" y="6.6" width="1.9" height="5" rx="0.95" fill={color} />
        </svg>
      </span>
      {open && <InfoModal title="Approximate location" body={LOCATION_HELP} onClose={() => setOpen(false)} />}
    </>
  )
}

export function InfoModal({ title, body, onClose }: { title: string; body: string; onClose: () => void }) {
  return (
    <div
      onClick={e => { e.stopPropagation(); onClose() }}
      style={{
        position: 'fixed', inset: 0, zIndex: 3000, background: 'rgba(0,0,0,.72)',
        display: 'flex', alignItems: 'center', justifyContent: 'center', padding: 20,
      }}
    >
      <div
        onClick={e => e.stopPropagation()}
        style={{
          background: '#101012', border: `1px solid ${BORDER}`, borderRadius: 14,
          padding: '22px 24px', maxWidth: 420, width: '100%', color: TEXT,
          boxShadow: '0 18px 60px rgba(0,0,0,.6)', position: 'relative',
        }}
      >
        <button onClick={onClose} aria-label="Close" style={{
          position: 'absolute', top: 10, right: 12, background: 'none', border: 'none',
          color: MUTED, fontSize: 16, cursor: 'pointer', lineHeight: 1,
        }}>✕</button>
        <div style={{ fontSize: 15, fontWeight: 700, marginBottom: 10 }}>{title}</div>
        <div style={{ fontSize: 13, color: '#c4c4cc', lineHeight: 1.55, whiteSpace: 'pre-line' }}>{body}</div>
      </div>
    </div>
  )
}

export function VerifiedBadge({ size = 20 }: { size?: number }) {
  const [open, setOpen] = useState(false)
  return (
    <>
      <span
        title="Verified node. Tap to learn more"
        onClick={e => { e.stopPropagation(); e.preventDefault(); setOpen(true) }}
        style={{
          background: BLUE, color: '#fff', borderRadius: '50%',
          width: size, height: size, display: 'inline-flex', alignItems: 'center',
          justifyContent: 'center', fontSize: size * 0.6, fontWeight: 700,
          flexShrink: 0, cursor: 'help',
        }}
      >✓</span>
      {open && <InfoModal title="Verified node ✓" body={VERIFIED_HELP} onClose={() => setOpen(false)} />}
    </>
  )
}

export function UnverifiedBadge({ size = 16 }: { size?: number }) {
  const [open, setOpen] = useState(false)
  return (
    <>
      <span
        title="Verification pending. Tap for how to get verified"
        onClick={e => { e.stopPropagation(); e.preventDefault(); setOpen(true) }}
        style={{
          position: 'relative', display: 'inline-block', fontSize: size,
          color: MUTED, fontWeight: 700, flexShrink: 0, cursor: 'help',
          lineHeight: 1, padding: '0 1px',
        }}
      >
        ✓
        <span style={{
          position: 'absolute', left: '-15%', right: '-15%', top: '48%',
          borderTop: '2px solid #e5484d', transform: 'rotate(45deg)',
        }} />
      </span>
      {open && <InfoModal title="Verification pending" body={VERIFY_HELP} onClose={() => setOpen(false)} />}
    </>
  )
}

export function GenesisBadge({ size = 18 }: { size?: number }) {
  const [open, setOpen] = useState(false)
  return (
    <>
      <span
        title="Genesis node. Tap to learn more"
        onClick={e => { e.stopPropagation(); e.preventDefault(); setOpen(true) }}
        style={{ fontSize: size, flexShrink: 0, cursor: 'help', lineHeight: 1 }}
      >⚡</span>
      {open && <InfoModal title="Genesis node ⚡" body={GENESIS_HELP} onClose={() => setOpen(false)} />}
    </>
  )
}

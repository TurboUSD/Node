"use client";

import { useEffect, useRef, useState } from "react";
import { useTusdLive, fmtUsd, fmtUsdCompact } from "./live";
import { COLORS, LINKS } from "./nav";
import { ExtArrow } from "./icons";

/**
 * The AMI eye and the treasury donut, as on turbousd.com: the ring is the supply (bought back,
 * staked, burned), the centre is the money AMI manages, and each opens a small popover.
 */
export default function TcAmiBadge({ up = false }: { up?: boolean }) {
  const [open, setOpen] = useState<"ami" | "donut" | null>(null);
  const root = useRef<HTMLDivElement>(null);
  const { live } = useTusdLive();

  useEffect(() => {
    const onDoc = (e: MouseEvent) => {
      if (root.current && !root.current.contains(e.target as Node)) setOpen(null);
    };
    document.addEventListener("click", onDoc);
    return () => document.removeEventListener("click", onDoc);
  }, []);

  const total = live?.supply.total ?? 100e9;
  const bought = live?.ops.boughtTusd ?? 0;
  const staked = live?.supply.staked ?? 0;
  const burned = live?.supply.burned ?? 0;
  const left = Math.max(1, total - burned);
  const t = live?.treasury;
  const rows = t
    ? [
        { k: "₸USD", v: t.tusdUsd, c: COLORS.tusd },
        { k: `Str. tokens (${t.strategicRows.length})`, v: t.strategicUsd, c: COLORS.strategic },
        { k: "WETH", v: t.wethUsd, c: COLORS.weth },
        { k: "USDC", v: t.usdc, c: COLORS.usdc },
      ]
    : [];
  const sum = rows.reduce((acc, r) => acc + r.v, 0);
  const parts = live
    ? [
        { v: bought, c: COLORS.bought },
        { v: staked, c: "#bdbdbd" },
        { v: burned, c: COLORS.burned },
        { v: Math.max(0, total - bought - staked - burned), c: "#5a5a5a" },
      ]
    : [{ v: 1, c: "#5a5a5a" }];
  const ring = live ? total || 1 : 1;
  const R = 16;
  const C = 2 * Math.PI * R;
  let acc = 0;
  const label = live ? fmtUsdCompact(sum || (t?.managedUsd ?? 0)).replace(".0", "") : "AMI";

  return (
    <div ref={root} className="tc-badge">
      <button
        type="button"
        aria-label="AMI (Artificial Monetary Intelligence)"
        aria-expanded={open === "ami"}
        onClick={() => setOpen((o) => (o === "ami" ? null : "ami"))}
        className={`tc-iconbtn ami-eye tc-eye ${open === "ami" ? "is-active" : ""}`}
      >
        <svg viewBox="0 0 136 286" className="tc-eye-svg" role="img" aria-hidden>
          <defs>
            <linearGradient id="tc-amiFrame" x1="0" y1="0" x2="1" y2="1">
              <stop offset="0%" stopColor="#f7f7f7" />
              <stop offset="18%" stopColor="#cfcfcf" />
              <stop offset="35%" stopColor="#8e8e8e" />
              <stop offset="50%" stopColor="#ececec" />
              <stop offset="68%" stopColor="#8a8a8a" />
              <stop offset="85%" stopColor="#d7d7d7" />
              <stop offset="100%" stopColor="#ffffff" />
            </linearGradient>
            <linearGradient id="tc-amiInner" x1="0" y1="0" x2="0" y2="1">
              <stop offset="0%" stopColor="#0d1c10" />
              <stop offset="100%" stopColor="#051108" />
            </linearGradient>
            <radialGradient id="tc-amiPanel" cx="50%" cy="55%" r="75%">
              <stop offset="0%" stopColor="#0f2a15" />
              <stop offset="55%" stopColor="#08210e" />
              <stop offset="100%" stopColor="#021006" />
            </radialGradient>
            <radialGradient id="tc-amiCore" cx="50%" cy="52%" r="52%">
              <stop offset="0%" stopColor="#dfff2c" />
              <stop offset="18%" stopColor="#9cff1f" />
              <stop offset="38%" stopColor="#24ff2f" />
              <stop offset="62%" stopColor="#00d31b" />
              <stop offset="82%" stopColor="#006e0b" />
              <stop offset="100%" stopColor="#045108" />
            </radialGradient>
            <radialGradient id="tc-amiOuter" cx="50%" cy="52%" r="60%">
              <stop offset="0%" stopColor="#d8ff42" stopOpacity="0.95" />
              <stop offset="28%" stopColor="#67ff29" stopOpacity="0.9" />
              <stop offset="58%" stopColor="#18e11d" stopOpacity="0.15" />
              <stop offset="100%" stopColor="#000000" stopOpacity="0" />
            </radialGradient>
            <radialGradient id="tc-amiRing" cx="35%" cy="30%" r="90%">
              <stop offset="0%" stopColor="#ffffff" />
              <stop offset="18%" stopColor="#d7d7d7" />
              <stop offset="38%" stopColor="#8d8d8d" />
              <stop offset="55%" stopColor="#fafafa" />
              <stop offset="72%" stopColor="#727272" />
              <stop offset="88%" stopColor="#d0d0d0" />
              <stop offset="100%" stopColor="#000000" />
            </radialGradient>
            <filter id="tc-amiSoft" x="-60%" y="-60%" width="220%" height="220%">
              <feGaussianBlur stdDeviation="10" result="blur" />
              <feMerge>
                <feMergeNode in="blur" />
                <feMergeNode in="SourceGraphic" />
              </feMerge>
            </filter>
            <filter id="tc-amiSmall" x="-50%" y="-50%" width="200%" height="200%">
              <feGaussianBlur stdDeviation="4" result="blur" />
              <feMerge>
                <feMergeNode in="blur" />
                <feMergeNode in="SourceGraphic" />
              </feMerge>
            </filter>
          </defs>
          <rect x="3" y="3" width="130" height="280" rx="10" fill="url(#tc-amiFrame)" />
          <rect x="13" y="13" width="110" height="260" rx="6" fill="url(#tc-amiInner)" />
          <rect x="13" y="13" width="110" height="260" rx="6" fill="url(#tc-amiPanel)" />
          <text x="68" y="42" fill="#fff" fontSize="18" fontFamily="Arial, Helvetica, sans-serif" fontWeight="700" textAnchor="middle" letterSpacing="2">
            AMI
          </text>
          <circle cx="68" cy="180" r="52" fill="url(#tc-amiRing)" />
          <circle cx="68" cy="180" r="43" fill="#000" />
          <circle cx="68" cy="180" r="39" fill="#0c1a0d" />
          <circle className="eye-glow" cx="68" cy="180" r="34" fill="url(#tc-amiOuter)" filter="url(#tc-amiSoft)" />
          <circle className="eye-glow-boost" cx="68" cy="180" r="48" fill="url(#tc-amiOuter)" filter="url(#tc-amiSoft)" />
          <circle className="eye-core" cx="68" cy="180" r="24" fill="url(#tc-amiCore)" />
          <circle className="eye-core-boost" cx="68" cy="180" r="36" fill="#6dff45" />
          <circle className="eye-fill-boost" cx="68" cy="180" r="31" fill="#9dff58" />
          <circle className="eye-center" cx="68" cy="180" r="7" fill="#dfff35" filter="url(#tc-amiSmall)" />
          <circle className="eye-center-boost" cx="68" cy="180" r="18" fill="#f0ff96" />
          <circle cx="68" cy="180" r="3" fill="#f5ff9d" />
          <circle cx="68" cy="180" r="51" fill="none" stroke="#fff" strokeWidth="2" opacity="0.45" />
          <circle cx="68" cy="180" r="38" fill="none" stroke="#72ff54" strokeWidth="1.5" opacity="0.35" />
        </svg>
      </button>

      <button
        type="button"
        aria-label="Treasury"
        aria-expanded={open === "donut"}
        onClick={() => setOpen((o) => (o === "donut" ? null : "donut"))}
        className="tc-iconbtn tc-donut"
      >
        <svg viewBox="0 0 40 40" width="40" height="40" role="img" aria-hidden>
          {parts.map((p, i) => {
            const len = (p.v / ring) * C;
            const el = (
              <circle
                key={i}
                cx="20"
                cy="20"
                r={R}
                fill="none"
                stroke={p.c}
                strokeWidth="3"
                strokeDasharray={`${len} ${C - len}`}
                strokeDashoffset={-acc}
                transform="rotate(-90 20 20)"
              />
            );
            acc += len;
            return el;
          })}
        </svg>
        <span className="tc-donut-label">{label}</span>
      </button>

      {open && (
        <div className={`tc-panel tc-pop ${up ? "tc-pop--up" : ""}`}>
          {open === "ami" ? (
            <>
              <p className="tc-eyebrow tc-eyebrow--turbo tc-pop-title">AMI 9000</p>
              {[
                ["How it works?", LINKS.ami],
                ["Managed funds", LINKS.dashboard],
                ["Talk to AMI", LINKS.telegram],
              ].map(([l, h]) => (
                <a key={l} href={h} className="tc-pop-link">
                  {l} <ExtArrow />
                </a>
              ))}
            </>
          ) : (
            <>
              <p className="tc-eyebrow tc-eyebrow--turbo tc-pop-title">Treasury Manager</p>
              {live ? (
                <>
                  <table className="tc-pop-table">
                    <tbody>
                      {rows.map((r) => (
                        <tr key={r.k}>
                          <td>
                            <span className="tc-sq" style={{ background: r.c }} />
                            {r.k}
                          </td>
                          <td className="tc-data tc-ar tc-ink">{fmtUsd(r.v)}</td>
                        </tr>
                      ))}
                      <tr className="tc-pop-total">
                        <td className="tc-muted">Total managed</td>
                        <td className="tc-data tc-ar tc-turbo">{fmtUsd(sum)}</td>
                      </tr>
                    </tbody>
                  </table>
                  <div className="tc-pop-grid">
                    {[
                      { k: "Burned", v: `${((burned / total) * 100).toFixed(2)}%`, c: COLORS.burned },
                      { k: "Staked", v: `${((staked / left) * 100).toFixed(2)}%`, c: COLORS.staked },
                      { k: "Bought", v: `${((bought / left) * 100).toFixed(2)}%`, c: COLORS.bought },
                    ].map((x) => (
                      <div key={x.k}>
                        <p className="tc-pop-k">
                          <span className="tc-dot" style={{ background: x.c }} />
                          {x.k}
                        </p>
                        <p className="tc-data tc-pop-v" style={{ color: x.c }}>
                          {x.v}
                        </p>
                      </div>
                    ))}
                  </div>
                </>
              ) : (
                <p className="tc-muted tc-pop-empty">Loading live numbers…</p>
              )}
              {[
                ["Dashboard & stats", LINKS.dashboard],
                ["Staking rewards", LINKS.staking],
              ].map(([l, h]) => (
                <a key={l} href={h} className="tc-pop-link">
                  {l} <ExtArrow />
                </a>
              ))}
            </>
          )}
        </div>
      )}
    </div>
  );
}

"use client";

/*
 * The turbousd.com footer for the subdomains: brand block, the four menu columns, the disclaimer,
 * and this site's "Open source on GitHub" link next to the copyright line.
 */
import { useState } from "react";
import { NAV, TUSD_ADDRESS, ROBINHOOD_ADDRESS } from "./nav";
import { Socials, GitHubIcon } from "./icons";
import TcAmiBadge from "./AmiBadge";

function Copy({ value }: { value: string }) {
  const [done, setDone] = useState(false);
  return (
    <button
      type="button"
      className={`tc-copy ${done ? "is-done" : ""}`}
      title={done ? "Copied" : "Copy"}
      aria-label="Copy address"
      onClick={async () => {
        try {
          await navigator.clipboard.writeText(value);
          setDone(true);
          setTimeout(() => setDone(false), 1600);
        } catch {
          /* clipboard blocked */
        }
      }}
    >
      {done ? (
        <svg width="14" height="14" viewBox="0 0 12 12" aria-hidden>
          <path d="M2 6.5l2.5 2.5L10 3.5" fill="none" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" strokeLinejoin="round" />
        </svg>
      ) : (
        <svg width="14" height="14" viewBox="0 0 12 12" aria-hidden>
          <rect x="4" y="4" width="7" height="7" rx="1.2" fill="none" stroke="currentColor" strokeWidth="1.2" />
          <path d="M8 4V2.5A1.5 1.5 0 006.5 1H2.5A1.5 1.5 0 001 2.5v4A1.5 1.5 0 002.5 8H4" fill="none" stroke="currentColor" strokeWidth="1.2" />
        </svg>
      )}
    </button>
  );
}

function Pill({ chain, address, href }: { chain: "base" | "robinhood"; address: string; href?: string }) {
  const short = `${address.slice(0, 6)}…${address.slice(-4)}`;
  const mark =
    chain === "base" ? (
      <svg width="18" height="18" viewBox="0 0 18 18" aria-label="Base" role="img">
        <rect width="18" height="18" rx="0.9" fill="#0000FF" />
      </svg>
    ) : (
      <svg width="18" height="18" viewBox="0 0 24 24" aria-label="Robinhood" role="img" fill="none" stroke="#00C805" strokeWidth="1.9" strokeLinecap="round" strokeLinejoin="round">
        <path d="M20.2 3.8c-3.2-1-7.6.2-10.3 2.9C7.2 9.4 6.2 13.1 6.6 16.4c3.3.4 7-.6 9.7-3.3 2.7-2.7 3.9-7.1 3.9-9.3z" />
        <path d="M16 8 4 20" />
        <path d="M11.5 12.5H16" />
      </svg>
    );
  const inner = (
    <>
      {mark}
      <span className="tc-pill-text">
        <span className="tc-mono tc-pill-chain">{chain === "base" ? "Base" : "Robinhood"}</span>
        <span className="tc-data tc-pill-addr">{short}</span>
      </span>
    </>
  );
  return (
    <div className="tc-pill">
      {href ? (
        <a href={href} target="_blank" rel="noreferrer" className="tc-pill-main">
          {inner}
        </a>
      ) : (
        <span className="tc-pill-main">{inner}</span>
      )}
      <Copy value={address} />
    </div>
  );
}

export default function TusdFooter({ github }: { github?: string }) {
  return (
    <footer className="tc-root tc-footer">
      <div className="tc-wrap">
        <div className="tc-foot-grid">
          <div className="tc-foot-brand">
            <a href="https://turbousd.com" aria-label="TurboUSD home">
              {/* eslint-disable-next-line @next/next/no-img-element */}
              <img src="/tusd-chrome/turbousd-logo.png" alt="TurboUSD" width={747} height={102} className="tc-foot-logo" />
            </a>
            <p className="tc-foot-tag">The World&apos;s First Unstablecoin. Deployed on Base on October 12, 2024. Embrace the Unstable ⚡️</p>
            <div className="tc-pills">
              <Pill chain="base" address={TUSD_ADDRESS} href={`https://basescan.org/token/${TUSD_ADDRESS}`} />
              <Pill chain="robinhood" address={ROBINHOOD_ADDRESS} />
            </div>
            <div className="tc-foot-icons">
              <Socials />
              <TcAmiBadge up />
            </div>
          </div>
          {NAV.map((g) => (
            <div key={g.label} className="tc-foot-col">
              <p className="tc-eyebrow tc-foot-head">{g.label}</p>
              <div className={g.columns.length > 1 ? "tc-foot-split" : ""}>
                {g.columns.map((col) => (
                  <div key={col.title ?? "all"}>
                    {col.title ? <p className="tc-mono tc-foot-sub">{col.title}</p> : null}
                    <ul>
                      {col.items.map((item) => (
                        <li key={item.label}>
                          <a href={item.href}>{item.label}</a>
                        </li>
                      ))}
                    </ul>
                  </div>
                ))}
              </div>
            </div>
          ))}
        </div>

        <div className="tc-disclaimer">
          <p className="tc-eyebrow tc-disc-head">Disclaimer</p>
          <div className="tc-disc-body">
            <p>
              TurboUSD is a token created for entertainment, cultural, and satirical purposes only and has no association with any
              currencies, stablecoins, stocks, or real life people. Any resemblance or association between TurboUSD and any of them is
              purely coincidental and intended for commentary, satire, or humorous expression. TurboUSD has no intrinsic value and
              carries no expectation of financial return. The project is fully community-driven, with no formal team or roadmap, and
              participation may result in the token becoming entirely worthless.
            </p>
            <details className="tc-details">
              <summary className="tc-mono">
                <span className="tc-when-closed">Read the full disclaimer</span>
                <span className="tc-when-open">Hide the full disclaimer</span>
                <svg width="9" height="9" viewBox="0 0 10 10" aria-hidden>
                  <path d="M2 3.5 5 6.5 8 3.5" fill="none" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" strokeLinejoin="round" />
                </svg>
              </summary>
              <p>
                TurboUSD including but not limited to associated tokens websites or marketing materials is not a licensed regulated
                exempted or recognized financial payment or investment service of any kind in any jurisdiction. Any terminology used on
                this website or related channels is intended solely for descriptive cultural or narrative purposes and carries no legal
                regulatory or financial meaning in a regulated environment. The TurboUSD smart contracts are open source permanent and
                non modifiable. This website and any TurboUSD related materials do not constitute a contract or contractual
                relationship of any kind nor do they represent an invitation solicitation or offer to invest speculate or acquire
                TurboUSD or any associated tokens with any expectation of profit. Any user of TurboUSD declares to have obtained
                appropriate technical administrative regulatory and legal advice before accessing this website or interacting with any
                part of the TurboUSD ecosystem and acknowledges the inherent high risk involved in accessing acquiring or using
                blockchain based systems tokens platforms software or interfaces. Furthermore the user expressly accepts that
                participation may result in partial or total loss and all community members and contributors involved directly or
                indirectly with TurboUSD disclaim any responsibility or liability arising from such use.
              </p>
            </details>
          </div>
          <div className="tc-foot-bottom">
            <p className="tc-mono tc-copyright">© {new Date().getFullYear()} TurboUSD · Embrace the Unstable</p>
            {github ? (
              <a href={github} target="_blank" rel="noreferrer" className="tc-github">
                <GitHubIcon width={16} height={16} />
                Open source on GitHub
              </a>
            ) : null}
          </div>
        </div>
      </div>
    </footer>
  );
}

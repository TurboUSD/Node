"use client";

/*
 * The turbousd.com header, shared by treasury.turbousd.com, store.turbousd.com and network.turbousd.com.
 * Three rows: the main site's menu (with the AMI eye, the treasury donut, Get ₸USD and, where the site
 * has a wallet, Connect), the live ticker, and a thin bar with this site's own sections.
 * Styles live in tusd-chrome.css under the `tc-` prefix, so they never touch the host site.
 */
import { useEffect, useRef, useState, type ComponentType, type ReactNode } from "react";
import { NAV, LINKS } from "./nav";
import { Socials } from "./icons";
import TcAmiBadge from "./AmiBadge";
import TcTicker from "./Ticker";

export type TcTab = { label: string; href: string; active?: boolean; external?: boolean };
type LinkProps = { href: string; className?: string; children?: ReactNode; onClick?: () => void };
type LinkLike = ComponentType<LinkProps>;

const PlainLink: LinkLike = (p) => <a {...p} />;

const FONTS =
  "https://fonts.googleapis.com/css2?family=JetBrains+Mono:wght@400;500;600&family=Poppins:wght@400;500;600;700&family=Space+Grotesk:wght@500;600;700&display=swap";

export default function TusdHeader({
  site,
  tabs,
  connect,
  subStyle = "default",
  Link,
}: {
  /** this site's name in the section bar, e.g. "Treasury" */
  site: { label: string; href: string };
  tabs: TcTab[];
  /** the wallet button, when the site has one */
  connect?: ReactNode;
  /** "centered": the section bar sits in the middle, with more air above it and no dot before the site name */
  subStyle?: "default" | "centered";
  /** the host's client-side link (next/link); plain <a> otherwise. Typed loosely: next/link's own props differ between Next versions. */
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  Link?: ComponentType<any>;
}) {
  const [open, setOpen] = useState(false);
  const [openGroup, setOpenGroup] = useState<string | null>(null);
  const L: LinkLike = (Link as LinkLike | undefined) ?? PlainLink;
  const sub = useRef<HTMLDivElement>(null);
  const activeHref = tabs.find((t) => t.active)?.href;

  // on a phone the section bar scrolls sideways: bring the current section into view
  useEffect(() => {
    const bar = sub.current;
    const el = bar?.querySelector<HTMLElement>(".tc-tab.is-active");
    if (!bar || !el) return;
    const end = el.offsetLeft + el.offsetWidth + 16;
    // only move when the tab would otherwise sit off-screen, so the site name stays visible when it can
    bar.scrollLeft = end > bar.clientWidth ? end - bar.clientWidth : 0;
  }, [activeHref]);

  useEffect(() => {
    if (!open) return;
    const onKey = (e: KeyboardEvent) => e.key === "Escape" && setOpen(false);
    document.addEventListener("keydown", onKey);
    return () => document.removeEventListener("keydown", onKey);
  }, [open]);

  return (
    <header className="tc-root tc-header">
      <link rel="stylesheet" href={FONTS} />
      <div className="tc-wrap tc-row">
        <a href={LINKS.home} className="tc-logo" aria-label="TurboUSD home">
          {/* eslint-disable-next-line @next/next/no-img-element */}
          <img src="/tusd-chrome/turbousd-logo.png" alt="TurboUSD" width={747} height={102} />
        </a>

        <nav className="tc-nav" aria-label="turbousd.com">
          {NAV.map((group) => (
            <div
              key={group.label}
              className="tc-group"
              onMouseEnter={() => setOpenGroup(group.label)}
              onMouseLeave={() => setOpenGroup((g) => (g === group.label ? null : g))}
            >
              <button
                type="button"
                className="tc-group-btn"
                aria-expanded={openGroup === group.label}
                onClick={() => setOpenGroup((g) => (g === group.label ? null : group.label))}
              >
                {group.label}
                <svg width="10" height="10" viewBox="0 0 10 10" aria-hidden>
                  <path d="M2 3.5l3 3 3-3" fill="none" stroke="currentColor" strokeWidth="1.4" />
                </svg>
              </button>
              <div className={`tc-drop ${group.columns.length > 1 ? "tc-drop--right" : ""} ${openGroup === group.label ? "is-open" : ""}`}>
                <div className="tc-panel tc-drop-panel">
                  {group.columns.map((col, i) => (
                    <div key={col.title ?? "all"} className={`tc-drop-col ${i > 0 ? "tc-drop-col--sep" : ""}`}>
                      {col.title ? <p className="tc-eyebrow tc-drop-title">{col.title}</p> : null}
                      {col.items.map((item) => (
                        <a key={item.label} href={item.href} className="tc-drop-item" onClick={() => setOpenGroup(null)}>
                          <span>{item.label}</span>
                          <span className="tc-mono tc-drop-blurb">{item.blurb}</span>
                        </a>
                      ))}
                    </div>
                  ))}
                </div>
              </div>
            </div>
          ))}
        </nav>

        <div className={`tc-right ${connect ? "tc-has-connect" : ""}`}>
          <span className="tc-socials-desk">
            <Socials />
          </span>
          <TcAmiBadge />
          <a href={LINKS.buy} className="tc-btn tc-btn--primary tc-btn--sm tc-desk">
            Get ₸USD
          </a>
          {connect ? <span className="tc-connect">{connect}</span> : null}
          <button
            type="button"
            className="tc-burger"
            aria-label={open ? "Close menu" : "Open menu"}
            aria-expanded={open}
            onClick={() => setOpen((o) => !o)}
          >
            <span className={open ? "is-open" : ""}>
              <i />
              <i />
              <i />
            </span>
          </button>
        </div>
      </div>

      {open ? (
        <div className="tc-drawer">
          {NAV.map((group) => (
            <div key={group.label} className="tc-drawer-group">
              <div className="tc-drawer-title">{group.label}</div>
              <div className="tc-drawer-grid">
                {group.columns.map((col) =>
                  group.columns.length > 1 ? (
                    <div key={col.title ?? "all"} className="tc-drawer-col">
                      {col.title ? <p className="tc-mono tc-drawer-sub">{col.title}</p> : null}
                      {col.items.map((item) => (
                        <a key={item.label} href={item.href} className="tc-drawer-link">
                          {item.label}
                        </a>
                      ))}
                    </div>
                  ) : (
                    col.items.map((item) => (
                      <a key={item.label} href={item.href} className="tc-drawer-link">
                        {item.label}
                      </a>
                    ))
                  ),
                )}
              </div>
            </div>
          ))}
          <div className="tc-drawer-foot">
            <Socials big />
            <a href={LINKS.buy} className="tc-btn tc-btn--primary tc-btn--sm">
              Get ₸USD
            </a>
          </div>
        </div>
      ) : null}

      <TcTicker />

      <div ref={sub} className={`tc-wrap tc-sub ${subStyle === "centered" ? "tc-sub--centered" : ""}`}>
        <L href={site.href} className="tc-sub-site">
          {subStyle === "centered" ? null : <span className="tc-sub-dot" />}
          {site.label}
        </L>
        <nav className="tc-sub-tabs" aria-label={site.label}>
          {tabs.map((t) =>
            t.external ? (
              <a key={t.href} href={t.href} className="tc-tab">
                {t.label}
              </a>
            ) : (
              <L key={t.href} href={t.href} className={`tc-tab ${t.active ? "is-active" : ""}`} onClick={() => setOpen(false)}>
                {t.label}
              </L>
            ),
          )}
        </nav>
      </div>
    </header>
  );
}

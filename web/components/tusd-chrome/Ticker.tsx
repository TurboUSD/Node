"use client";

import { type ReactNode } from "react";
import { useTusdLive, fmtCompact, fmtInt, fmtPct, fmtPrice, fmtUsdCompact, timeAgo } from "./live";
import { ExtArrow } from "./icons";

type Item = { k: string; v: ReactNode; tone?: "up" | "down" | "fiat" | "burn" | "turbo"; href?: string };

/** The live strip under the menu, same items and order as on turbousd.com. Pauses on hover. */
export default function TcTicker() {
  const { live, macro } = useTusdLive();
  const last = live?.ops.last ?? null;
  const dash = "—";

  const items: Item[] = [
    { k: "₸USD", v: live ? fmtPrice(live.priceUsd) : dash, tone: live && live.change24h < 0 ? "down" : "up" },
    { k: "24h", v: live ? fmtPct(live.change24h) : dash, tone: live && live.change24h < 0 ? "down" : "up" },
    { k: "MCAP", v: live ? fmtUsdCompact(live.marketCapUsd) : dash },
    { k: "Burned", v: live ? `${fmtCompact(live.supply.burned)} ₸` : dash, tone: "burn" },
    { k: "Bought back", v: live ? `${fmtCompact(live.ops.boughtTusd)} ₸` : dash, tone: "turbo" },
    { k: "Staked", v: live ? `${fmtCompact(live.supply.staked)} ₸` : dash },
    { k: "Wallets", v: live ? fmtInt(live.holders) : dash },
    { k: "AMI managed", v: live ? fmtUsdCompact(live.treasury.managedUsd) : dash },
    {
      k: "Last AMI op",
      v: last ? (
        <>
          {last.op_type} · {timeAgo(last.date_utc)} <ExtArrow />
        </>
      ) : (
        dash
      ),
      href: last ? `https://basescan.org/tx/${last.tx_hash}` : undefined,
      tone: last && (last.op_type === "Burn" || last.op_type === "BurnEngine") ? "burn" : "turbo",
    },
    ...(macro
      ? ([
          { k: "USD printed", v: `+$${fmtInt(macro.latest.m2PerSecond)}/s`, tone: "fiat" },
          { k: "M2 YoY", v: fmtPct(macro.latest.m2YoYPct, 1), tone: "fiat" },
        ] as Item[])
      : []),
  ];

  const row = items.map((it, i) => (
    <span key={it.k + i} className="tc-tick">
      <span className="tc-tick-k">{it.k}</span>
      {it.href ? (
        <a href={it.href} target="_blank" rel="noreferrer" className={`tc-data tc-tick-v tc-tick-a tc-tone-${it.tone ?? "ink"}`}>
          {it.v}
        </a>
      ) : (
        <span className={`tc-data tc-tick-v tc-tone-${it.tone ?? "ink"}`}>{it.v}</span>
      )}
      <span className="tc-tick-sep">·</span>
    </span>
  ));

  return (
    <div className="tc-wrap">
      <div className="tc-ticker tc-mono" aria-label="Live market data">
        <span className="tc-live">
          <span className="tc-live-dot" />
          <span>LIVE</span>
        </span>
        <div className="tc-ticker-view">
          <div className="tc-ticker-track">
            {row}
            {row}
          </div>
        </div>
      </div>
    </div>
  );
}

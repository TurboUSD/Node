"use client";

/*
 * Live numbers for the shared turbousd.com header and footer. They come from turbousd.com itself
 * (/api/live and /api/macro, both CORS-open), so the ticker and the treasury donut on this site always
 * show exactly what the main site shows. One fetch per minute is shared by every component on the page.
 */
import { useEffect, useState } from "react";

export const TUSD_ORIGIN = "https://turbousd.com";

/*
 * The API is read from whichever host serves the site directly. Vercel redirects one of turbousd.com /
 * www.turbousd.com to the other, and a cross-origin fetch fails on a redirect (the 308 carries no CORS
 * header), so both are tried and the one that answers is remembered.
 */
const API_ORIGINS = ["https://www.turbousd.com", "https://turbousd.com"];
let apiOrigin: string | null = null;

export interface TusdLive {
  priceUsd: number;
  change24h: number;
  marketCapUsd: number;
  holders: number;
  supply: { total: number; burned: number; staked: number };
  treasury: {
    managedUsd: number;
    strategicUsd: number;
    wethUsd: number;
    usdc: number;
    tusdUsd: number;
    strategicRows: Array<{ ticker: string; valueUsd: number }>;
  };
  ops: {
    boughtTusd: number;
    last: { op_type: string; date_utc: string; tx_hash: string } | null;
  };
}

export interface TusdMacro {
  latest: { m2PerSecond: number; m2YoYPct: number };
}

type State = { live: TusdLive | null; macro: TusdMacro | null };

let state: State = { live: null, macro: null };
const subs = new Set<(s: State) => void>();
let timer: ReturnType<typeof setInterval> | null = null;
let started = false;

async function getJson<T>(path: string): Promise<T | null> {
  const origins = apiOrigin ? [apiOrigin, ...API_ORIGINS.filter((o) => o !== apiOrigin)] : API_ORIGINS;
  for (const origin of origins) {
    try {
      const res = await fetch(origin + path, { cache: "no-store" });
      if (!res.ok) continue;
      const json = (await res.json()) as T;
      apiOrigin = origin;
      return json;
    } catch {
      /* redirect or network error: try the other host */
    }
  }
  return null;
}

async function refresh(withMacro: boolean) {
  const [live, macro] = await Promise.all([
    getJson<TusdLive>("/api/live"),
    withMacro ? getJson<TusdMacro>("/api/macro") : Promise.resolve(null),
  ]);
  state = { live: live ?? state.live, macro: macro ?? state.macro };
  subs.forEach((fn) => fn(state));
}

function start() {
  if (started) return;
  started = true;
  void refresh(true);
  timer = setInterval(() => void refresh(!state.macro), 60_000);
}

export function useTusdLive(): State {
  const [s, setS] = useState<State>(state);
  useEffect(() => {
    subs.add(setS);
    start();
    setS(state);
    return () => {
      subs.delete(setS);
      if (subs.size === 0 && timer) {
        clearInterval(timer);
        timer = null;
        started = false;
      }
    };
  }, []);
  return s;
}

/* formatters: the same ones turbousd.com uses */
const compact = new Intl.NumberFormat("en-US", { notation: "compact", maximumFractionDigits: 2 });
const compact1 = new Intl.NumberFormat("en-US", { notation: "compact", maximumFractionDigits: 1 });
const usdFmt = new Intl.NumberFormat("en-US", { style: "currency", currency: "USD", maximumFractionDigits: 0 });
const intFmt = new Intl.NumberFormat("en-US");

export const fmtCompact = (n: number) => (Number.isFinite(n) ? compact.format(n) : "—");
export const fmtUsd = (n: number) => (Number.isFinite(n) ? usdFmt.format(n) : "—");
export const fmtUsdCompact = (n: number) =>
  !Number.isFinite(n) ? "—" : Math.abs(n) < 1000 ? usdFmt.format(n) : "$" + compact1.format(n);
export const fmtInt = (n: number) => (Number.isFinite(n) ? intFmt.format(Math.round(n)) : "—");
export function fmtPrice(n: number) {
  if (!Number.isFinite(n) || n <= 0) return "—";
  if (n >= 1) return "$" + n.toFixed(2);
  const digits = Math.min(10, Math.max(2, -Math.floor(Math.log10(n)) + 3));
  return "$" + n.toFixed(digits);
}
export function fmtPct(n: number, digits = 2) {
  if (!Number.isFinite(n)) return "—";
  const s = n.toFixed(digits) + "%";
  return n > 0 ? "+" + s : s;
}
export function timeAgo(iso: string) {
  const m = Math.floor((Date.now() - new Date(iso).getTime()) / 60000);
  if (m < 1) return "just now";
  if (m < 60) return `${m}m ago`;
  const h = Math.floor(m / 60);
  if (h < 48) return `${h}h ago`;
  return `${Math.floor(h / 24)}d ago`;
}

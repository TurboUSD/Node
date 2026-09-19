/* The turbousd.com menu, with absolute links so it works from every subdomain. Keep in sync with
   src/lib/constants.ts (NAV) in the tusd_web repo. */
export const T = "https://turbousd.com";
export const TUSD_ADDRESS = "0x3d5e487b21e0569048c4d1a60e98c36e1b09db07";
export const ROBINHOOD_ADDRESS = "0x0000000000000000000000000000000000000000";

export const LINKS = {
  home: T,
  buy: `${T}/buy`,
  staking: `${T}/staking`,
  ami: `${T}/artificial-monetary-intelligence`,
  dashboard: "https://treasury.turbousd.com",
  telegram: "https://t.me/turbo_usd",
  x: "https://x.com/turbousd",
  dexscreener: `https://dexscreener.com/base/${TUSD_ADDRESS}`,
  uniswap: "https://app.uniswap.org/swap?outputCurrency=0x3d5e487B21E0569048c4D1A60E98C36e1B09DB07&chain=base",
};

export type NavItem = { label: string; href: string; blurb: string };
export type NavGroup = { label: string; columns: Array<{ title: string | null; items: NavItem[] }> };

const CULTURE: NavItem[] = [
  { label: "Manifesto", href: `${T}/manifiesto`, blurb: "Why we reject stability" },
  { label: "Unstable Meta", href: `${T}/unstable-meta`, blurb: "The sector we started" },
  { label: "Community & Builders", href: `${T}/community`, blurb: "Cult of Chaos" },
  { label: "Timeline & News", href: `${T}/timeline`, blurb: "What happened" },
  { label: "Database", href: `${T}/memes`, blurb: "Memes" },
];
const PRODUCTS: NavItem[] = [
  { label: "Store", href: "https://store.turbousd.com", blurb: "Merch" },
  { label: "TurboX Energy", href: `${T}/turbox`, blurb: "The drink" },
  { label: "Turbo Node", href: "https://network.turbousd.com/node", blurb: "Desktop IoT device" },
  { label: "Brand Kit", href: `${T}/brand`, blurb: "Logos & assets" },
];

export const NAV: NavGroup[] = [
  {
    label: "Economic Core",
    columns: [
      {
        title: null,
        items: [
          { label: "₸USD", href: `${T}/unstablecoin`, blurb: "The unstablecoin" },
          { label: "Origin", href: `${T}/origin`, blurb: "Oct 12, 2024" },
          { label: "The Flywheel", href: `${T}/#flywheel`, blurb: "From demand to burn" },
          { label: "Staking", href: `${T}/staking`, blurb: "~15% APY" },
          { label: "Stats", href: `${T}/stats`, blurb: "Live numbers" },
        ],
      },
    ],
  },
  {
    label: "Tokenized Assets",
    columns: [
      {
        title: null,
        items: [
          { label: "The Anchor", href: `${T}/#tokenized-assets`, blurb: "Stocks, metals & crypto" },
          { label: "Tokenization", href: `${T}/#tokenization`, blurb: "SEC, CFTC & the onchain wave" },
          { label: "Strategic Tokens", href: `${T}/#treasury`, blurb: "Buyback triggers" },
          { label: "Holdings", href: "https://treasury.turbousd.com", blurb: "Live, onchain" },
        ],
      },
    ],
  },
  {
    label: "Monetary AI",
    columns: [
      {
        title: null,
        items: [
          { label: "AMI Overview", href: `${T}/artificial-monetary-intelligence`, blurb: "The anti-Fed" },
          { label: "Agent Architecture", href: `${T}/artificial-monetary-intelligence#architecture`, blurb: "How AMI thinks" },
          { label: "Treasury Manager", href: `${T}/treasury-manager`, blurb: "Onchain execution" },
        ],
      },
    ],
  },
  {
    label: "Brand & Culture",
    columns: [
      { title: "Culture", items: CULTURE },
      { title: "Products", items: PRODUCTS },
    ],
  },
];

export const COLORS = {
  tusd: "#00ca6a",
  weth: "#8b5cf6",
  usdc: "#3b82f6",
  strategic: "#d63384",
  bought: "#9ce0ff",
  staked: "#d9d9d9",
  burned: "#ff5a5a",
};

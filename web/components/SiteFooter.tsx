// components/SiteFooter.tsx — the turbousd.com footer, rendered on every page (app/layout.tsx), with
// the "Open source on GitHub" link to the Node repo.

import { TusdFooter } from './tusd-chrome'

export default function SiteFooter() {
  return <TusdFooter github="https://github.com/TurboUSD/Node" />
}

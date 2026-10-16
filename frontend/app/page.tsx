import type { Metadata } from 'next';
import TrinityWorkspace from '@/components/trinity/TrinityWorkspace';

/**
 * The workspace is the product, so `/` is the app rather than a landing page:
 * the command bar and its live result fill the viewport, and the bridged
 * engine consoles sit directly underneath as the next thing you can run.
 *
 * The narrative content that used to live here — hero, system flow, engine
 * grid, project showcase, philosophy — moved to `/systems` and `/about`
 * rather than being deleted, and the navbar points at those routes.
 */

export const metadata: Metadata = {
  title: 'Trinity AI — Engineering Operating System',
  description:
    'Turn natural-language engineering requirements into structured specifications, geometry, validation data and production-ready artifacts — deterministically.',
};

export default function Home() {
  return (
    <main className="app-shell">
      <TrinityWorkspace />
    </main>
  );
}
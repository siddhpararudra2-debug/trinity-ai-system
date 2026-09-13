import type { Metadata } from 'next';
import Hero from '@/components/trinity/Hero';
import SystemFlow from '@/components/trinity/SystemFlow';
import TrinityWorkspace from '@/components/trinity/TrinityWorkspace';
import EngineeringSystems from '@/components/trinity/EngineeringSystems';
import Projects from '@/components/trinity/Projects';
import Philosophy from '@/components/trinity/Philosophy';
import Reveal from '@/components/sections/Reveal';

export const metadata: Metadata = {
  title: 'Trinity AI — Engineering Operating System',
  description:
    'Turn natural-language engineering requirements into structured specifications, geometry, validation data and production-ready artifacts — deterministically.',
};

export default function Home() {
  return (
    <main>
      <Hero />
      <TrinityWorkspace />
      <Reveal>
        <SystemFlow />
      </Reveal>
      <Reveal delay={80}>
        <EngineeringSystems />
      </Reveal>
      <Reveal delay={80}>
        <Projects />
      </Reveal>
      <Reveal delay={80}>
        <Philosophy />
      </Reveal>

      {/* Engine registry strip — honest secondary band */}
      <section className="engine-strip" aria-label="Registered engines">
        <div className="container engine-strip-grid">
          <div className="strip-label">Engine registry — one process, structured capabilities</div>
          <div className="strip-item is-live">
            <strong>CAD</strong>
            <small>● generate · validate</small>
          </div>
          <div className="strip-item is-live">
            <strong>MATH</strong>
            <small>● solve · evaluate</small>
          </div>
          <div className="strip-item">
            <strong>PCB</strong>
            <small>SCAFFOLD · 501</small>
          </div>
          <div className="strip-item">
            <strong>VISION</strong>
            <small>SCAFFOLD · 501</small>
          </div>
        </div>
      </section>
    </main>
  );
}

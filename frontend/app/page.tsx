import type { Metadata } from 'next';
import Hero from '@/components/trinity/Hero';
import EngineStrip from '@/components/trinity/EngineStrip';
import SystemFlow from '@/components/trinity/SystemFlow';
import TrinityWorkspace from '@/components/trinity/TrinityWorkspace';
import BridgedEngines from '@/components/trinity/BridgedEngines';
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
      <TrinityWorkspace />
      <BridgedEngines />
      <Reveal variant="up">
        <Hero />
      </Reveal>
      <Reveal variant="up">
        <SystemFlow />
      </Reveal>
      <Reveal variant="fade" delay={60}>
        <EngineeringSystems />
      </Reveal>
      <Reveal variant="up" delay={60}>
        <Projects />
      </Reveal>
      <Reveal variant="fade" delay={60}>
        <Philosophy />
      </Reveal>

      {/* Engine registry strip — live from GET /api/engines, static fallback */}
      <EngineStrip />
    </main>
  );
}

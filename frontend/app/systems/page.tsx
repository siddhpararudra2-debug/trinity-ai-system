import type { Metadata } from 'next';
import Hero from '@/components/trinity/Hero';
import SystemFlow from '@/components/trinity/SystemFlow';
import EngineeringSystems from '@/components/trinity/EngineeringSystems';
import EngineStrip from '@/components/trinity/EngineStrip';
import Reveal from '@/components/sections/Reveal';

export const metadata: Metadata = {
  title: 'The System — Trinity AI',
  description:
    'How Trinity routes a requirement through deterministic engines: the flow, the engine roster and what each one actually returns.',
};

/**
 * Everything the workspace homepage used to carry as marketing sections. It
 * moved here when `/` became the app so the narrative is one click away
 * instead of sitting between you and the tool.
 */
export default function SystemsPage() {
  return (
    <main>
      <Reveal variant="up">
        <Hero />
      </Reveal>
      <Reveal variant="up">
        <SystemFlow />
      </Reveal>
      <Reveal variant="fade" delay={60}>
        <EngineeringSystems />
      </Reveal>

      {/* Engine registry strip — live from GET /api/engines, static fallback */}
      <EngineStrip />
    </main>
  );
}
'use client';

import { useEffect, useState } from 'react';
import { getEngines, type EngineInfo } from '@/lib/api/trinity';

/**
 * Registry strip driven by GET /api/engines when the backend is reachable,
 * static fallback when it is not (offline build, no backend running).
 * A scaffolded/bridged engine never renders as LIVE — the status pill
 * follows the API's `status` field, which Stage 2 made machine-honest.
 */

const FALLBACK: EngineInfo[] = [
  { name: 'cad', version: '1.0', capabilities: ['generate', 'validate'] },
  { name: 'math', version: '1.0', capabilities: ['solve', 'evaluate'] },
  { name: 'pcb', version: '0.1', capabilities: [], status: 'scaffolded' },
  { name: 'vision', version: '0.1', capabilities: [], status: 'scaffolded' },
];

function Pill({ info }: { info: EngineInfo }) {
  const live = (info.status ?? 'live') === 'live';
  const caps = info.capabilities.slice(0, 2).join(' · ');
  return (
    <div className={`strip-item ${live ? 'is-live' : ''}`}>
      <strong>{info.name.toUpperCase()}</strong>
      <small>
        {live ? `● ${caps}` : `${(info.status ?? 'scaffold').toUpperCase()} · 501`}
      </small>
    </div>
  );
}

export default function EngineStrip() {
  const [engines, setEngines] = useState<EngineInfo[]>(FALLBACK);

  useEffect(() => {
    let cancelled = false;
    getEngines().then((rows) => {
      if (!cancelled && rows && rows.length > 0) setEngines(rows.slice(0, 8));
    });
    return () => {
      cancelled = true;
    };
  }, []);

  return (
    <section className="engine-strip" aria-label="Registered engines">
      <div className="container engine-strip-grid">
        <div className="strip-label">Engine registry — one process, structured capabilities</div>
        {engines.map((info) => (
          <Pill key={info.name} info={info} />
        ))}
      </div>
    </section>
  );
}

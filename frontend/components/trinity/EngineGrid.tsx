'use client';

import { useEffect, useState } from 'react';
import Reveal from '@/components/sections/Reveal';
import { getEngines, type EngineInfo } from '@/lib/api/trinity';

export type EngineCard = {
  id: string;
  name: string;
  version: string;
  capabilities: string[];
  body: string;
  facts: string[];
};

type Props = {
  fallback: EngineCard[];
};

/**
 * Engine registry grid driven by GET /api/engines when reachable.
 * Live rows keep the editorial body/facts; the status pill follows the
 * API so a bridged or newly-live engine can never render stale copy.
 */
export default function EngineGrid({ fallback }: Props) {
  const [live, setLive] = useState<Record<string, EngineInfo> | null>(null);

  useEffect(() => {
    let cancelled = false;
    getEngines().then((rows) => {
      if (cancelled || !rows) return;
      const byName: Record<string, EngineInfo> = {};
      for (const row of rows) byName[row.name] = row;
      setLive(byName);
    });
    return () => {
      cancelled = true;
    };
  }, []);

  return (
    <div className="workspace-grid">
      {fallback.map((engine, i) => {
        const info = live?.[engine.id];
        const status = info ? (info.status ?? 'live') : null;
        const isLive = status === null ? engine.id === 'cad' || engine.id === 'math' : status === 'live';
        const pill = status === null ? (isLive ? 'LIVE' : 'SCAFFOLD') : status === 'live' ? 'LIVE' : status.toUpperCase();
        return (
          <Reveal key={engine.id} delay={(i % 2) * 70}>
            <article className="ws-panel" id={engine.id} style={{ height: '100%' }}>
              <h2>
                {engine.name} · {info?.version ?? engine.version}
              </h2>
              <p className="ws-empty">{engine.body}</p>
              <ul className="ws-checks" style={{ marginTop: 16 }}>
                {engine.facts.map((fact) => (
                  <li key={fact}>
                    <b>▸</b> {fact}
                  </li>
                ))}
              </ul>
              <div style={{ display: 'flex', gap: 8, flexWrap: 'wrap', marginTop: 18 }}>
                <span className={`status-pill ${isLive ? 'is-live' : 'is-scaffold'}`}>
                  {pill}
                </span>
                {(info?.capabilities ?? engine.capabilities).map((cap) => (
                  <span className="status-pill" key={cap}>
                    {cap}
                  </span>
                ))}
              </div>
            </article>
          </Reveal>
        );
      })}
    </div>
  );
}

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
 *
 * Editorial body/facts are local copy and stay. The status pill and the
 * capability chips are claims about the running system, so they come only
 * from the API. This used to guess -- cad and math were hardcoded LIVE and
 * every other engine SCAFFOLDED before the fetch returned, which painted
 * six fully implemented bridged engines as scaffolds, and because the fetch
 * had no rejection path that guess became permanent if the API was down.
 * Unknown now renders as unknown.
 */
export default function EngineGrid({ fallback }: Props) {
  const [live, setLive] = useState<Record<string, EngineInfo> | null>(null);
  const [unreachable, setUnreachable] = useState(false);

  useEffect(() => {
    let cancelled = false;
    getEngines()
      .then((rows) => {
        if (cancelled || !rows) return;
        const byName: Record<string, EngineInfo> = {};
        for (const row of rows) byName[row.name] = row;
        setLive(byName);
      })
      .catch(() => {
        if (!cancelled) setUnreachable(true);
      });
    return () => {
      cancelled = true;
    };
  }, []);

  return (
    <div className="workspace-grid">
      {fallback.map((engine, i) => {
        const info = live?.[engine.id];
        const isLive = info ? (info.status ?? 'live') === 'live' : false;
        const pill = !info
          ? unreachable
            ? 'UNREACHABLE'
            : 'UNKNOWN'
          : isLive
            ? 'LIVE'
            : (info.status ?? 'scaffold').toUpperCase();
        const caps = info?.capabilities ?? [];
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
                {caps.map((cap) => (
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

'use client';

import { useEffect, useState } from 'react';
import { getEngines, type EngineInfo } from '@/lib/api/trinity';

/**
 * Registry strip driven by GET /api/engines.
 *
 * There is deliberately no hardcoded fallback table. It used to carry one,
 * and it was wrong in both directions: it marked pcb and vision as
 * "scaffolded · 501" when both are fully implemented bridged engines, and
 * it would have marked cad and math LIVE with no way to have checked. The
 * engine status this component shows is the one claim in the UI that comes
 * from a single API call, so when that call fails the honest answer is to
 * say the registry is unreachable -- not to publish a stale guess that
 * happens to look authoritative.
 */

type LoadState = { status: 'loading' | 'ready' | 'unreachable'; rows: EngineInfo[] };

/**
 * Status means three different things and the label has to say which:
 *   live       in-process Python engine (cad, math)
 *   bridged    fully implemented, reached through the native subprocess --
 *              it works, so printing "501" next to it is a lie
 *   scaffolded a genuine stub, and the one status that does mean 501
 */
function Pill({ info }: { info: EngineInfo }) {
  const status = info.status ?? 'live';
  const caps = info.capabilities.slice(0, 2).join(' · ');
  const unimplemented = status === 'scaffolded';
  return (
    <div className={`strip-item ${unimplemented ? '' : 'is-live'}`}>
      <strong>{info.name.toUpperCase()}</strong>
      <small>
        {unimplemented
          ? 'SCAFFOLDED · 501'
          : status === 'live'
            ? `● ${caps}`
            : `BRIDGED · ${caps}`}
      </small>
    </div>
  );
}

export default function EngineStrip() {
  const [state, setState] = useState<LoadState>({ status: 'loading', rows: [] });

  useEffect(() => {
    let cancelled = false;
    getEngines()
      .then((rows) => {
        if (cancelled) return;
        if (rows && rows.length > 0) {
          setState({ status: 'ready', rows: rows.slice(0, 8) });
        } else {
          setState({ status: 'unreachable', rows: [] });
        }
      })
      .catch(() => {
        if (!cancelled) setState({ status: 'unreachable', rows: [] });
      });
    return () => {
      cancelled = true;
    };
  }, []);

  return (
    <section className="engine-strip" aria-label="Registered engines">
      <div className="container engine-strip-grid">
        <div className="strip-label">Engine registry — one process, structured capabilities</div>
        {state.status === 'ready' ? (
          state.rows.map((info) => <Pill key={info.name} info={info} />)
        ) : (
          <div className="strip-item is-unreachable" style={{ gridColumn: 'span 4' }}>
            <strong>
              {state.status === 'loading' ? 'QUERYING REGISTRY…' : 'REGISTRY UNREACHABLE'}
            </strong>
            <small>
              {state.status === 'loading'
                ? 'GET /api/engines'
                : 'engine status is unknown, not reported as live'}
            </small>
          </div>
        )}
      </div>
    </section>
  );
}
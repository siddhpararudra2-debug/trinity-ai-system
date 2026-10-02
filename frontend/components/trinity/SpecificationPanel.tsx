'use client';

import type { TrinityResult } from '@/lib/api/trinity';

function labelFor(name: string): string {
  return name.replaceAll('_', ' ').replace(/\b\w/g, (letter) => letter.toUpperCase());
}

function formatValue(name: string, value: unknown, unit?: string): string {
  const hasUnit = unit && unit !== 'count' && !/(^|_)(count|quantity|motors?)($|_)/i.test(name);
  if (typeof value === 'number') {
    const rendered = Number.isInteger(value) ? String(value) : value.toFixed(2);
    return hasUnit ? `${rendered} ${unit}` : rendered;
  }
  if (Array.isArray(value)) return value.map((part) => String(part)).join(' × ');
  return value === null || value === undefined ? '—' : String(value);
}

export default function SpecificationPanel({ result }: { result: TrinityResult | null }) {
  const spec = result?.result.spec;
  const parameters = spec?.parameters as Record<string, unknown> | undefined;
  const parameterRows = parameters ? Object.entries(parameters) : [];
  const partName = String(result?.result.type ?? spec?.type ?? 'CAD part');
  const triangleCount = result?.result.triangle_count;
  const bounds = result?.result.bounding_box_mm;
  const boundsText = Array.isArray(bounds)
    ? bounds.map((value) => Array.isArray(value) ? value.join(', ') : String(value)).join(' — ')
    : bounds && typeof bounds === 'object'
      ? `${JSON.stringify((bounds as Record<string, unknown>).min ?? '')} — ${JSON.stringify((bounds as Record<string, unknown>).max ?? '')}`
      : undefined;
  const hasData = !!result?.success && parameterRows.length > 0;

  return (
    <section className="spec-panel" aria-labelledby="spec-title">
      <div className="spec-panel-head">
        <h3 id="spec-title">ENGINEERING SPECIFICATION</h3>
        <span className={`status-pill ${hasData ? 'is-live' : ''}`}>{hasData ? 'LIVE RESULT' : 'AWAITING'}</span>
      </div>

      {!hasData ? (
        <div style={{ padding: '20px' }}>
          <p className="spec-empty">
            Generate a catalog part to inspect its parameters and geometry checks here.
          </p>
          <div className="spec-group">
            <div className="spec-group-title">CAD OUTPUT</div>
            <dl style={{ margin: 0 }}>
              <div className="spec-row"><dt>Part</dt><dd>—</dd></div>
              <div className="spec-row"><dt>Triangle count</dt><dd>—</dd></div>
              <div className="spec-row"><dt>Validation</dt><dd>AWAITING</dd></div>
            </dl>
          </div>
        </div>
      ) : (
        <>
          <div className="spec-group">
            <div className="spec-group-title">{labelFor(partName)} — PARAMETERS</div>
            <dl style={{ margin: 0 }}>
              {parameterRows.map(([key, value]) => (
                <div className="spec-row" key={key}>
                  <dt>{labelFor(key)}</dt>
                  <dd className="mono-num">{formatValue(key, value, spec?.units)}</dd>
                </div>
              ))}
            </dl>
          </div>
          <div className="spec-group">
            <div className="spec-group-title">GEOMETRY</div>
            <dl style={{ margin: 0 }}>
              {typeof triangleCount === 'number' && (
                <div className="spec-row"><dt>Triangle count</dt><dd className="mono-num">{triangleCount.toLocaleString()}</dd></div>
              )}
              {boundsText && <div className="spec-row"><dt>Bounds (mm)</dt><dd className="mono-num">{boundsText}</dd></div>}
              <div className="spec-row"><dt>Validation</dt><dd>{result?.validation?.status ?? 'GENERATED'}</dd></div>
              {result?.result.parse?.matched_rule && (
                <div className="spec-row">
                  <dt>Matched rule</dt>
                  <dd>{typeof result.result.parse.matched_rule === 'string' ? result.result.parse.matched_rule : result.result.parse.matched_rule.name ?? '—'}</dd>
                </div>
              )}
            </dl>
          </div>
          <div style={{ padding: '12px 20px', borderTop: '1px solid #F1F1ED', display: 'flex', justifyContent: 'space-between', fontFamily: 'var(--font-mono-plex), monospace', fontSize: '0.62rem', letterSpacing: '0.08em', color: '#707070' }}>
            <span>JOB {result?.job_id.slice(0, 8)}</span>
            <span>{result?.engine.toUpperCase()} · {result?.operation.toUpperCase()}</span>
          </div>
        </>
      )}
    </section>
  );
}

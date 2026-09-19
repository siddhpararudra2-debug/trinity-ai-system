'use client';

import type { TrinityResult } from '@/lib/api/trinity';

const CHECK_LABELS: Record<string, string> = {
  dimensional_accuracy: 'Dimensional accuracy',
  topology: 'Topology',
  symmetry: 'Symmetry',
  geometry_integrity: 'Geometry integrity',
  manufacturability: 'Manufacturability',
  dimensions: 'Dimensions',
  clearances: 'Clearances',
  residuals: 'Residuals',
};

function pretty(k: string) {
  return CHECK_LABELS[k] ?? k.replaceAll('_', ' ');
}

export default function ValidationPanel({ result }: { result: TrinityResult | null }) {
  const validation = result?.validation;

  if (!validation) {
    return (
      <section className="validation-panel" aria-labelledby="validation-title">
        <div className="validation-head">
          <h3 id="validation-title">VALIDATION ENGINE</h3>
          <p style={{ margin: '8px 0 0', fontSize: '0.82rem', color: '#707070', lineHeight: 1.6 }}>
            Every geometry leaves with a verdict.
          </p>
        </div>
        <div style={{ padding: '20px' }}>
          <p style={{ margin: 0, fontSize: '0.84rem', color: '#707070', lineHeight: 1.6 }}>
            Dimensional accuracy, topology, symmetry and manufacturability checks
            appear here — validation is independent of generation.
          </p>
          <ul className="validation-checks" style={{ marginTop: 16, opacity: 0.6 }}>
            {['Dimensional accuracy', 'Topology', 'Symmetry', 'Geometry integrity', 'Manufacturability'].map((l) => (
              <li key={l}>
                <span>{l}</span>
                <b style={{ color: '#707070' }}>READY</b>
              </li>
            ))}
          </ul>
        </div>
      </section>
    );
  }

  const isPassed = validation.status === 'VALIDATED' || validation.status === 'VERIFIED';
  const isFailed = validation.status === 'FAILED';

  return (
    <section className="validation-panel" aria-labelledby="validation-title">
      <div className="validation-head">
        <h3 id="validation-title">VALIDATION ENGINE</h3>
        <p style={{ margin: '8px 0 0', fontSize: '0.82rem', color: '#707070', lineHeight: 1.6 }}>
          Every geometry leaves with a verdict.
        </p>
      </div>

      <div className={`validation-status ${isPassed ? 'is-pass' : isFailed ? 'is-failed' : ''}`}>
        <span
          style={{
            width: 8,
            height: 8,
            borderRadius: '50%',
            background: isPassed ? '#2D6A4F' : isFailed ? '#B42318' : '#D0D0CA',
            flex: '0 0 auto',
          }}
          aria-hidden="true"
        />
        {validation.status}
        <span style={{ marginLeft: 'auto', fontSize: '0.62rem', opacity: 0.7 }}>
          {result?.engine.toUpperCase()}
        </span>
      </div>

      <ul className="validation-checks">
        {Object.entries(validation.checks)
          .slice(0, 8)
          .map(([k, v]) => {
            const pass = v === true;
            const fail = v === false;
            return (
              <li key={k} className={pass ? 'is-pass' : fail ? 'is-fail' : ''}>
                <span>{pretty(k)}</span>
                <b>{pass ? 'PASS' : fail ? 'FAILED' : String(v).toUpperCase()}</b>
              </li>
            );
          })}
        {Object.keys(validation.checks).length === 0 && (
          <li>
            <span>Checks</span>
            <b>NO DATA</b>
          </li>
        )}
      </ul>

      <div style={{ padding: '12px 20px', fontFamily: 'var(--font-mono-plex), monospace', fontSize: '0.62rem', color: '#707070', borderTop: '1px solid #F1F1ED' }}>
        Independent of generation · {Object.keys(validation.checks).length} checks
      </div>
    </section>
  );
}

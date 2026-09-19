'use client';

import type { TrinityResult } from '@/lib/api/trinity';

export default function SpecificationPanel({ result }: { result: TrinityResult | null }) {
  const params = result?.result.spec?.parameters as Record<string, number> | undefined;
  const triangleCount = result?.result.triangle_count as number | undefined;

  const hasData = !!params && Object.keys(params).length > 0;
  const dims = params
    ? {
        'Frame width': params.overall_size ?? params.frame_width ?? params.width,
        'Frame height': params.overall_size ?? params.frame_height ?? params.height,
        'Arm thickness': params.arm_thickness ?? params.thickness ?? 3.4,
        'Hub diameter': params.hub_diameter ?? 8,
        'Overall size': params.overall_size,
      }
    : null;

  const geometry = {
    Topology: 'QUADCOPTER',
    Symmetry: 'RADIAL',
    Configuration: 'X-FRAME',
    'Triangle count': triangleCount ? `${triangleCount.toLocaleString()}` : '—',
  };

  return (
    <section className="spec-panel" aria-labelledby="spec-title">
      <div className="spec-panel-head">
        <h3 id="spec-title">ENGINEERING SPECIFICATION</h3>
        <span className={`status-pill ${hasData ? 'is-live' : ''}`}>{hasData ? 'LIVE RESULT' : 'DEMO'}</span>
      </div>

      {!hasData ? (
        <div style={{ padding: '20px' }}>
          <p className="spec-empty">
            Submit a requirement — structured parameters appear here with 2-decimal
            precision. Values use <span className="mono">IBM Plex Mono</span> with
            tabular numerals.
          </p>
          <div className="spec-group">
            <div className="spec-group-title">Dimensions — Example (DEMO)</div>
            <dl style={{ margin: 0 }}>
              <div className="spec-row">
                <dt>Frame width</dt>
                <dd className="mono-num">50.00 mm</dd>
              </div>
              <div className="spec-row">
                <dt>Frame height</dt>
                <dd className="mono-num">50.00 mm</dd>
              </div>
              <div className="spec-row">
                <dt>Arm thickness</dt>
                <dd className="mono-num">2.00 mm</dd>
              </div>
              <div className="spec-row">
                <dt>Hub diameter</dt>
                <dd className="mono-num">8.00 mm</dd>
              </div>
            </dl>
          </div>
          <div className="spec-group">
            <div className="spec-group-title">Geometry — Example</div>
            <dl style={{ margin: 0 }}>
              <div className="spec-row">
                <dt>Topology</dt>
                <dd>QUADCOPTER</dd>
              </div>
              <div className="spec-row">
                <dt>Symmetry</dt>
                <dd>RADIAL</dd>
              </div>
              <div className="spec-row">
                <dt>Status</dt>
                <dd>AWAITING</dd>
              </div>
            </dl>
          </div>
        </div>
      ) : (
        <>
          <div className="spec-group">
            <div className="spec-group-title">Dimensions</div>
            <dl style={{ margin: 0 }}>
              {dims &&
                Object.entries(dims)
                  .filter(([, v]) => v !== undefined)
                  .map(([k, v]) => (
                    <div className="spec-row" key={k}>
                      <dt>{k}</dt>
                      <dd className="mono-num">{typeof v === 'number' ? `${v.toFixed(2)} mm` : String(v)}</dd>
                    </div>
                  ))}
            </dl>
          </div>
          <div className="spec-group">
            <div className="spec-group-title">Geometry</div>
            <dl style={{ margin: 0 }}>
              {Object.entries(geometry).map(([k, v]) => (
                <div className="spec-row" key={k}>
                  <dt>{k}</dt>
                  <dd className="mono-num">{v}</dd>
                </div>
              ))}
              {params &&
                Object.entries(params)
                  .filter(([k]) => !['overall_size', 'frame_width', 'frame_height', 'width', 'height', 'arm_thickness', 'thickness', 'hub_diameter'].includes(k))
                  .map(([k, v]) => (
                    <div className="spec-row" key={k}>
                      <dt>{k.replaceAll('_', ' ')}</dt>
                      <dd className="mono-num">{typeof v === 'number' ? v.toFixed(2) : String(v)}</dd>
                    </div>
                  ))}
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

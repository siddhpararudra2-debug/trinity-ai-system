'use client';

import { useState } from 'react';
import type { ExecutionStage, TrinityResult } from '@/lib/api/trinity';
import EngineeringCommand from './EngineeringCommand';
import SpecificationPanel from './SpecificationPanel';
import GeometryViewer from './GeometryViewer';
import ValidationPanel from './ValidationPanel';
import ArtifactPanel from './ArtifactPanel';

export default function TrinityWorkspace() {
  const [result, setResult] = useState<TrinityResult | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [stage, setStage] = useState<ExecutionStage>('idle');

  const handleResult = (r: TrinityResult | null, e: string | null) => {
    setResult(r);
    setError(e);
  };

  return (
    <>
      <EngineeringCommand onResult={handleResult} onStageChange={setStage} />

      {/* LIVE RESULT — spec | viewer | validation */}
      <section className="live-section" id="live-result" aria-labelledby="live-result-title">
        <div className="container">
          <div className="section-label">03 / LIVE RESULT — 04 / VALIDATION</div>
          <div className="systems-head" style={{ marginBottom: 32 }}>
            <div>
              <h2 id="live-result-title">Specification. Geometry. Verdict.</h2>
            </div>
            <p className="lede">
              One envelope: <span className="mono">result</span> ·{' '}
              <span className="mono">artifacts</span> ·{' '}
              <span className="mono">validation</span> ·{' '}
              <span className="mono">job_id</span>. No faked engineering data.
              {stage !== 'idle' && stage !== 'complete' ? (
                <span style={{ display: 'inline-flex', marginLeft: 8, padding: '4px 8px', border: '1px solid #315B73', background: '#E8EEF2', color: '#315B73', fontFamily: 'var(--font-mono-plex), monospace', fontSize: '0.62rem', letterSpacing: '0.08em' }}>
                  {stage.toUpperCase()}
                </span>
              ) : null}
            </p>
          </div>

          <div className="live-grid">
            <SpecificationPanel result={result} />
            <GeometryViewer result={result} />
            <ValidationPanel result={result} />
          </div>
        </div>
      </section>

      <ArtifactPanel result={result} error={error} />
    </>
  );
}

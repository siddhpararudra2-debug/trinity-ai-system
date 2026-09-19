'use client';

import { useState } from 'react';

type Artifact = { artifact_id: string; type: string; size_bytes: number };
type Result = {
  success: boolean;
  result: { spec?: { parameters: Record<string, number> }; triangle_count?: number };
  artifacts: Artifact[];
  validation?: { status: string; checks: Record<string, unknown> };
  errors: { message: string }[];
};

const API = process.env.NEXT_PUBLIC_TRINITY_API_URL ?? 'http://localhost:8000/api';

const errorResult = (message: string): Result => ({
  success: false,
  result: {},
  artifacts: [],
  errors: [{ message }],
});

export default function Workspace() {
  const [prompt, setPrompt] = useState('Create a 50 mm quadcopter frame');
  const [result, setResult] = useState<Result | null>(null);
  const [busy, setBusy] = useState(false);

  async function execute() {
    setBusy(true);
    setResult(null);
    try {
      const response = await fetch(`${API}/requirements/execute`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ text: prompt }),
      });
      const payload: unknown = await response.json().catch(() => null);
      if (!response.ok) {
        const apiError = payload as { detail?: string; errors?: { message?: string }[] } | null;
        throw new Error(
          apiError?.errors?.[0]?.message ?? apiError?.detail ?? `Request failed (${response.status})`,
        );
      }
      if (!payload || typeof payload !== 'object' || !('success' in payload)) {
        throw new Error('The API returned an invalid response.');
      }
      setResult(payload as Result);
    } catch (error) {
      setResult(errorResult(error instanceof Error ? error.message : 'Unable to execute the engineering request.'));
    } finally {
      setBusy(false);
    }
  }

  const params = result?.result.spec?.parameters;

  return (
    <>
      <div className="workspace-grid">
        <div className="ws-command">
          <label htmlFor="engineering-requirement">Engineering requirement</label>
          <div className="ws-command-row">
            <input
              id="engineering-requirement"
              className="ws-input"
              value={prompt}
              onChange={(e) => setPrompt(e.target.value)}
              placeholder="Create a 50 mm quadcopter frame"
              spellCheck={false}
            />
            <button className="ws-button" onClick={execute} disabled={busy}>
              {busy ? 'Executing…' : 'Execute'}
            </button>
          </div>
        </div>

        <section className="ws-panel">
          <h2>Specification</h2>
          {params ? (
            <dl>
              {Object.entries(params).map(([k, v]) => (
                <div className="ws-spec-row" key={k}>
                  <dt>{k.replaceAll('_', ' ')}</dt>
                  <dd>{v} mm</dd>
                </div>
              ))}
            </dl>
          ) : (
            <p className="ws-empty">
              Submit a V1 frame requirement to build structured CAD parameters — the requirement
              router maps it onto the CAD IR deterministically.
            </p>
          )}
        </section>

        <section className="ws-panel">
          <h2>3D preview</h2>
          <div className="ws-frame">
            ◈<small>{result?.success ? 'GLB mesh generated' : 'Awaiting geometry'}</small>
          </div>
          <p className="ws-empty" style={{ marginTop: 12 }}>
            {result?.result.triangle_count
              ? `${result.result.triangle_count} triangles · GLB ready for Three.js`
              : 'Orbit viewer attaches to the generated GLB artifact.'}
          </p>
        </section>

        <section className="ws-panel">
          <h2>Validation</h2>
          {result?.validation ? (
            <>
              <strong className={`ws-validation-status ${result.validation.status === 'FAILED' ? 'is-failed' : ''}`}>
                {result.validation.status}
              </strong>
              <ul className="ws-checks">
                {Object.entries(result.validation.checks)
                  .slice(0, 6)
                  .map(([k, v]) => (
                    <li key={k}>
                      <b>{v === true ? '✓' : v === false ? '✗' : '•'}</b> {k.replaceAll('_', ' ')}
                    </li>
                  ))}
              </ul>
            </>
          ) : (
            <p className="ws-empty">
              Geometry, dimensions, topology and manufacturability checks appear here — validation
              is independent of generation.
            </p>
          )}
        </section>

        <section className="ws-panel">
          <h2>Artifacts</h2>
          {result?.artifacts?.length ? (
            <ul style={{ margin: 0, padding: 0, listStyle: 'none' }}>
              {result.artifacts.map((a) => (
                <li className="ws-artifact" key={a.artifact_id}>
                  <a href={`${API}/artifacts/${a.artifact_id}`} target="_blank" rel="noopener noreferrer">
                    {a.type.toUpperCase()}
                  </a>
                  <span>{a.size_bytes.toLocaleString()} bytes</span>
                </li>
              ))}
            </ul>
          ) : (
            <p className="ws-empty">
              STL, GLB and JSON artifacts include persistent provenance and SHA-256 checksums.
            </p>
          )}
        </section>

        {result?.errors?.length ? (
          <p className="ws-error">
            <strong style={{ color: 'var(--danger)' }}>EXECUTION ERROR · </strong>
            {result.errors[0].message}
          </p>
        ) : null}
      </div>
    </>
  );
}

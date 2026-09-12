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
const errorResult = (message: string): Result => ({ success: false, result: {}, artifacts: [], errors: [{ message }] });

export default function Home() {
  const [prompt, setPrompt] = useState('Create a 50 mm quadcopter frame');
  const [result, setResult] = useState<Result | null>(null);
  const [busy, setBusy] = useState(false);
  async function execute() {
    setBusy(true); setResult(null);
    try {
      const response = await fetch(`${API}/requirements/execute`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ text: prompt }) });
      const payload: unknown = await response.json().catch(() => null);
      if (!response.ok) {
        const apiError = payload as { detail?: string; errors?: { message?: string }[] } | null;
        throw new Error(apiError?.errors?.[0]?.message ?? apiError?.detail ?? `Request failed (${response.status})`);
      }
      if (!payload || typeof payload !== 'object' || !('success' in payload)) throw new Error('The API returned an invalid response.');
      setResult(payload as Result);
    } catch (error) {
      setResult(errorResult(error instanceof Error ? error.message : 'Unable to execute the engineering request.'));
    } finally { setBusy(false); }
  }
  const params = result?.result.spec?.parameters;
  return <main className="workspace"><header><b>TRINITY AI</b><span>ENGINEERING OPERATING SYSTEM</span><i>● SYSTEM READY</i></header><section className="command"><label htmlFor="engineering-requirement">Engineering requirement</label><div><input id="engineering-requirement" value={prompt} onChange={e => setPrompt(e.target.value)} /><button onClick={execute} disabled={busy}>{busy ? 'Executing…' : 'Execute'}</button></div></section><div className="grid"><section className="panel"><h2>Specification</h2>{params ? <dl>{Object.entries(params).map(([k, v]) => <div key={k}><dt>{k.replaceAll('_', ' ')}</dt><dd>{v} mm</dd></div>)}</dl> : <p>Submit a V1 frame requirement to build structured CAD parameters.</p>}</section><section className="panel viewer"><h2>3D preview</h2><div className="frame">◈<small>{result?.success ? 'GLB mesh generated' : 'Awaiting geometry'}</small></div><p>{result?.result.triangle_count ? `${result.result.triangle_count} triangles · GLB ready for Three.js` : 'Orbit viewer attaches to the generated GLB artifact.'}</p></section><section className="panel"><h2>Validation</h2>{result?.validation ? <><strong>{result.validation.status}</strong><ul>{Object.entries(result.validation.checks).slice(0, 5).map(([k, v]) => <li key={k}>{v === true ? '✓' : '•'} {k.replaceAll('_', ' ')}</li>)}</ul></> : <p>Geometry, dimensions, topology and manufacturability checks appear here.</p>}</section><section className="panel"><h2>Artifacts</h2>{result?.artifacts?.length ? <ul>{result.artifacts.map(a => <li key={a.artifact_id}><a href={`${API}/artifacts/${a.artifact_id}`} target="_blank">{a.type.toUpperCase()}</a><span>{a.size_bytes.toLocaleString()} bytes</span></li>)}</ul> : <p>STL, GLB and JSON artifacts include persistent provenance and checksums.</p>}</section></div>{result?.errors?.length ? <p className="error">{result.errors[0].message}</p> : null}</main>;
}

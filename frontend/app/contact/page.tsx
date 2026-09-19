import type { Metadata } from 'next';
import Reveal from '@/components/sections/Reveal';

export const metadata: Metadata = {
  title: 'Run It',
  description:
    'Run Trinity AI locally: start the FastAPI core, launch the Next.js workspace, and execute the flagship CAD request in minutes.',
};

const steps = [
  {
    n: '01',
    title: 'Start the core',
    body: 'The FastAPI backend boots the storage layout, SQLite schema and engine registry in one lifespan hook.',
    code: 'python -m pip install -e .\nuvicorn src.main:app --reload --port 8000',
  },
  {
    n: '02',
    title: 'Launch the workspace',
    body: 'The Next.js frontend talks to the core on :8000 — override with NEXT_PUBLIC_TRINITY_API_URL.',
    code: 'npm --prefix frontend install\nnpm --prefix frontend run dev',
  },
  {
    n: '03',
    title: 'Execute the flagship request',
    body: 'Or drive the API directly — every engine call returns the same envelope with validation and artifacts.',
    code: "curl -X POST localhost:8000/api/cad/generate \\\n  -H 'Content-Type: application/json' \\\n  -d '{\"parameters\":{\"overall_size\":50},\"outputs\":[\"stl\",\"glb\",\"json\"]}'",
  },
];

const endpoints = [
  ['GET', '/api/health', 'Liveness check'],
  ['GET', '/api/engines', 'Registered capabilities'],
  ['POST', '/api/cad/generate', 'Flagship CAD generation'],
  ['POST', '/api/math/solve', 'Verified symbolic solving'],
  ['GET', '/api/jobs/{job_id}', 'Job status + result'],
  ['GET', '/api/artifacts/{artifact_id}', 'Checksummed artifact download'],
];

export default function ContactPage() {
  return (
    <main>
      <section className="page-hero">
        <div className="container">
          <p className="eyebrow">Run it locally</p>
          <h1>
            The whole system runs on <span className="accent-word">your machine.</span>
          </h1>
          <p className="lede">
            No cloud, no keys, no queue broker. Trinity&rsquo;s V1 core is a FastAPI app, a SQLite
            file and this workspace — deterministic end to end.
          </p>
        </div>
      </section>

      <section className="section-sm" aria-label="Quickstart">
        <div className="container">
          <div className="workspace-grid">
            {steps.map((step, i) => (
              <Reveal key={step.n} delay={(i % 2) * 70}>
                <article className="ws-panel" style={{ height: '100%' }}>
                  <h2>
                    {step.n} · {step.title}
                  </h2>
                  <p className="ws-empty">{step.body}</p>
                  <div className="code-block">
                    <code>{step.code}</code>
                  </div>
                </article>
              </Reveal>
            ))}
          </div>
        </div>
      </section>

      <section className="section-sm" aria-labelledby="endpoints-title">
        <div className="container">
          <div className="section-head">
            <Reveal>
              <div>
                <p className="eyebrow">API surface</p>
                <h2 id="endpoints-title">Eight endpoints, one envelope.</h2>
              </div>
            </Reveal>
            <Reveal>
              <p className="lede">
                Check success in the body, not just the status code — engine-time failures come back
                as tracked failed jobs by design.
              </p>
            </Reveal>
          </div>

          <Reveal>
            <div className="compare">
              <div className="compare-row compare-head">
                <div>Method</div>
                <div>Path</div>
                <div>Purpose</div>
              </div>
              {endpoints.map(([method, path, purpose]) => (
                <div className="compare-row" key={path}>
                  <div>
                    <span className={method === 'GET' ? 'partial' : 'yes'}>{method}</span>
                  </div>
                  <div style={{ color: 'var(--text)' }}>
                    <code className="mono">{path}</code>
                  </div>
                  <div className="partial">{purpose}</div>
                </div>
              ))}
            </div>
          </Reveal>

          <Reveal>
            <p className="lede" style={{ marginTop: 44 }}>
              Repository: <code className="mono">github.com/siddhpararudra2-debug/trinity-ai-system</code>{' '}
              — architecture docs live in <code className="mono">docs/</code>, backend tests in{' '}
              <code className="mono">tests/</code>.
            </p>
          </Reveal>
        </div>
      </section>
    </main>
  );
}

import type { Metadata } from 'next';
import Reveal from '@/components/sections/Reveal';

export const metadata: Metadata = {
  title: 'Stack & Status',
  description:
    'The technology behind Trinity AI: FastAPI, SQLite, SymPy, a pure-Python mesh kernel, a dependency-free glTF exporter and a Next.js workspace — with honest status for each.',
};

const stack = [
  {
    layer: 'API',
    name: 'FastAPI + Pydantic',
    detail: 'Typed request edge, structured errors, thread-offloaded execution.',
    status: 'LIVE',
  },
  {
    layer: 'PERSISTENCE',
    name: 'SQLite (WAL, no ORM)',
    detail: 'Jobs, artifacts, validations, cache entries — metadata only, never large files.',
    status: 'LIVE',
  },
  {
    layer: 'MATH KERNEL',
    name: 'SymPy',
    detail: 'Symbolic solve/evaluate with root re-substitution as independent verification.',
    status: 'LIVE',
  },
  {
    layer: 'CAD KERNEL',
    name: 'Native mesh builder',
    detail: 'Pure-Python primitives, deterministic box geometry, binary STL + glTF 2.0 GLB writers.',
    status: 'LIVE',
  },
  {
    layer: 'WORKFLOWS',
    name: 'DAG primitives',
    detail: 'Topological ordering with duplicate and cycle rejection — the seam for multi-engine pipelines.',
    status: 'PRIMITIVES',
  },
  {
    layer: 'FRONTEND',
    name: 'Next.js + React',
    detail: 'This workspace: executes the flagship request against the real API.',
    status: 'LIVE',
  },
  {
    layer: 'CI',
    name: 'pytest + ruff + eslint',
    detail: 'Backend regression coverage, lint gates and a frontend build check on every push.',
    status: 'LIVE',
  },
  {
    layer: 'CAD KERNELS',
    name: 'CadQuery / FreeCAD / Onshape',
    detail: 'Adapters behind the same four-method interface. STEP stays a 501 until configured.',
    status: 'NEXT',
  },
  {
    layer: 'INTELLIGENCE',
    name: 'Model provider boundary',
    detail: 'Structured tool calls only. The execution core never gains a model dependency.',
    status: 'ISOLATED',
  },
];

export default function TeamPage() {
  return (
    <main>
      <section className="page-hero">
        <div className="container">
          <p className="eyebrow">Stack &amp; status</p>
          <h1>
            Small stack. <span className="accent-word">Zero pretence.</span>
          </h1>
          <p className="lede">
            Every layer is named, inspectable and status-honest. Nothing here is a roadmap dressed
            up as a feature — NEXT means not built, ISOLATED means deliberately out of the core.
          </p>
        </div>
      </section>

      <section className="section-sm" aria-label="Stack layers">
        <div className="container">
          <div className="stack-grid">
            {stack.map((item, i) => (
              <Reveal key={item.name} delay={(i % 3) * 70}>
                <article className="stack-card" style={{ height: '100%' }}>
                  <p className="telemetry" style={{ marginBottom: 12 }}>
                    {item.layer}
                  </p>
                  <h3>{item.name}</h3>
                  <p>{item.detail}</p>
                  <p style={{ marginTop: 14 }}>
                    <span
                      className={`status-pill ${
                        item.status === 'LIVE'
                          ? 'is-live'
                          : item.status === 'NEXT'
                            ? 'is-scaffold'
                            : ''
                      }`}
                    >
                      {item.status}
                    </span>
                  </p>
                </article>
              </Reveal>
            ))}
          </div>
        </div>
      </section>

      <section className="section-sm" aria-labelledby="truth-title">
        <div className="container">
          <div className="section-head">
            <Reveal>
              <div>
                <p className="eyebrow">Verification states</p>
                <h2 id="truth-title">Four words, used precisely.</h2>
              </div>
            </Reveal>
            <Reveal>
              <p className="lede">
                The validation status vocabulary is part of the API contract — a result is only
                VERIFIED when an independent check has confirmed it.
              </p>
            </Reveal>
          </div>

          <div className="workspace-grid">
            {[
              {
                s: 'GENERATED',
                d: 'Geometry or a result exists. No independent check has passed yet.',
                cls: '',
              },
              {
                s: 'VALIDATED',
                d: 'The validator scored the output — geometry checks or root residuals all passed.',
                cls: 'is-live',
              },
              {
                s: 'VERIFIED',
                d: 'Re-execution or identity confirmation agrees. The strongest state.',
                cls: 'is-pass',
              },
              {
                s: 'FAILED',
                d: 'A check failed. The job is tracked, the error classified, nothing stored as output.',
                cls: 'is-failed',
              },
            ].map((item, i) => (
              <Reveal key={item.s} delay={i * 60}>
                <article className="ws-panel" style={{ height: '100%' }}>
                  <h2>Status</h2>
                  <strong
                    className={`ws-validation-status ${item.s === 'FAILED' ? 'is-failed' : ''}`}
                    style={{ fontSize: '1.2rem' }}
                  >
                    {item.s}
                  </strong>
                  <p className="ws-empty" style={{ marginTop: 14 }}>
                    {item.d}
                  </p>
                </article>
              </Reveal>
            ))}
          </div>
        </div>
      </section>
    </main>
  );
}

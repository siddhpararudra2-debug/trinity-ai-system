import type { Metadata } from 'next';
import Link from 'next/link';
import Reveal from '@/components/sections/Reveal';

export const metadata: Metadata = {
  title: 'Architecture',
  description:
    'How Trinity AI is wired: API → structured request → job manager → engine registry → engine → validation → artifact manager → SQLite lineage.',
};

const layers = [
  {
    n: '01',
    name: 'API edge',
    detail: 'FastAPI + Pydantic. Structured requests only — no free-form strings reach an engine.',
    status: 'LIVE',
  },
  {
    n: '02',
    name: 'Job manager',
    detail: 'Each engine call becomes a tracked job: queued → running → completed | failed, in SQLite with WAL.',
    status: 'LIVE',
  },
  {
    n: '03',
    name: 'Engine registry',
    detail: 'One process-wide registry. Engines register real capability lists the API can enumerate.',
    status: 'LIVE',
  },
  {
    n: '04',
    name: 'Engines',
    detail: 'CAD (native mesh kernel) and Math (SymPy) are live; six truthful scaffolds are registered behind them.',
    status: 'LIVE',
  },
  {
    n: '05',
    name: 'Validation',
    detail: 'Independent of generation: geometry checks for CAD, residual re-substitution for math. GENERATED → VERIFIED.',
    status: 'LIVE',
  },
  {
    n: '06',
    name: 'Artifact manager',
    detail: 'The only writer to artifact storage. Server-generated IDs, SHA-256 checksums, job provenance.',
    status: 'LIVE',
  },
];

const phases = [
  {
    n: 'PHASE 1',
    title: 'Persistent primitives',
    body: 'Jobs, artifacts, cache and the engine registry — SQLite-backed, no broker, no ORM.',
  },
  {
    n: 'PHASE 2',
    title: 'Deterministic CAD',
    body: 'Parametric quadcopter frame through a native mesh builder with binary STL, GLB and JSON export.',
  },
  {
    n: 'PHASE 3',
    title: 'Workflow primitives',
    body: 'DAG ordering and explicit verification states, ready for multi-engine pipelines.',
  },
  {
    n: 'PHASE 4',
    title: 'Truthful scaffolds',
    body: 'PCB, firmware, vision, research, simulation and robotics registered — refusing to fake output until real.',
  },
];

export default function AboutPage() {
  return (
    <main>
      <section className="page-hero">
        <div className="container">
          <p className="eyebrow">Architecture</p>
          <h1>
            One pipeline. <span className="accent-word">No shortcuts.</span>
          </h1>
          <p className="lede">
            API → structured request → job manager → engine registry → engine → validation →
            artifact manager → SQLite lineage. A future model provider can emit structured tool
            calls — it never executes engines itself.
          </p>
        </div>
      </section>

      <section className="section-sm" aria-label="System layers">
        <div className="container">
          <div className="workspace-grid">
            {layers.map((layer, i) => (
              <Reveal key={layer.n} delay={i * 60}>
                <article className="ws-panel" style={{ height: '100%' }}>
                  <h2>
                    {layer.n} · {layer.name}
                  </h2>
                  <p className="ws-empty">{layer.detail}</p>
                  <p style={{ marginTop: 16 }}>
                    <span className={`status-pill ${layer.status === 'LIVE' ? 'is-live' : ''}`}>
                      {layer.status}
                    </span>
                  </p>
                </article>
              </Reveal>
            ))}
          </div>
        </div>
      </section>

      <section className="section" aria-labelledby="audit-title">
        <div className="container">
          <div className="section-head">
            <Reveal>
              <div>
                <p className="eyebrow">Audit trail</p>
                <h2 id="audit-title">What was preserved, what was rebuilt.</h2>
              </div>
            </Reveal>
            <Reveal>
              <p className="lede">
                The architecture audit found synchronous handlers, missing GLB, no cache or workflow
                primitives and a disconnected frontend. The current build closes those gaps without
                inventing capabilities.
              </p>
            </Reveal>
          </div>

          <div className="principles">
            {[
              {
                n: 'FIXED',
                title: 'Thread-offloaded execution',
                body: 'Handlers no longer run CPU-bound engine work on the event loop.',
              },
              {
                n: 'FIXED',
                title: 'Native GLB exporter',
                body: 'A dependency-free, standards-compliant glTF 2.0 binary writer with regression tests.',
              },
              {
                n: 'FIXED',
                title: 'Deterministic cache',
                body: 'SQLite cache for pure operations — CAD generation deliberately excluded to preserve provenance.',
              },
              {
                n: 'FIXED',
                title: 'Trinity workspace UI',
                body: 'This frontend executes the flagship request against the real API.',
              },
            ].map((item, i) => (
              <Reveal key={item.n + item.title} delay={i * 70}>
                <article className="principle">
                  <span>{item.n}</span>
                  <h3>{item.title}</h3>
                  <p>{item.body}</p>
                </article>
              </Reveal>
            ))}
          </div>
        </div>
      </section>

      <section className="section-sm" aria-labelledby="phases-title">
        <div className="container">
          <div className="section-head">
            <Reveal>
              <div>
                <p className="eyebrow">Phases</p>
                <h2 id="phases-title">Built in order, admitted in order.</h2>
              </div>
            </Reveal>
            <Reveal>
              <p className="lede">
                The intelligence layer is an isolated future boundary — nothing in the V1 core
                depends on it.
              </p>
            </Reveal>
          </div>

          <div className="process-grid">
            {phases.map((phase, i) => (
              <Reveal key={phase.n} delay={i * 70}>
                <article className="process-step">
                  <div className="step-num">
                    <span>{String(i + 1).padStart(2, '0')}</span>
                    <span>{phase.n}</span>
                  </div>
                  <h3>{phase.title}</h3>
                  <p>{phase.body}</p>
                </article>
              </Reveal>
            ))}
          </div>

          <Reveal>
            <div style={{ marginTop: 40 }}>
              <Link className="btn btn-ghost" href="/#workspace">
                See it execute <span aria-hidden="true">→</span>
              </Link>
            </div>
          </Reveal>
        </div>
      </section>
    </main>
  );
}

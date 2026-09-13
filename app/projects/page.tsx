import type { Metadata } from 'next';
import Link from 'next/link';
import Reveal from '@/components/sections/Reveal';

export const metadata: Metadata = {
  title: 'Artifacts & Jobs',
  description:
    'Trinity AI artifacts: checksummed STL, GLB and JSON outputs with job provenance, validation records and SQLite lineage.',
};

const artifacts = [
  {
    type: 'STL',
    name: 'Binary stereolithography',
    body: '80-byte header, little-endian float32 triangles with computed normals — written by the native exporter, no third-party writer.',
    tag: 'MESH',
  },
  {
    type: 'GLB',
    name: 'glTF 2.0 binary',
    body: 'Dependency-free, standards-compliant exporter with POSITION and NORMAL accessors, min/max bounds and 4-byte alignment.',
    tag: 'VIEWER-READY',
  },
  {
    type: 'JSON',
    name: 'CAD IR spec',
    body: 'The exact intermediate representation behind the mesh: every parameter, unit and default that produced the geometry.',
    tag: 'PROVENANCE',
  },
];

const lifecycle = [
  { n: '01', name: 'QUEUED', detail: 'Request validated at the Pydantic edge, job row inserted' },
  { n: '02', name: 'RUNNING', detail: 'Engine executes off the event loop in scratch space' },
  { n: '03', name: 'VALIDATED', detail: 'Independent checks recorded in the validations table' },
  { n: '04', name: 'STORED', detail: 'Artifacts copied into managed storage with SHA-256 checksums' },
  { n: '05', name: 'COMPLETED', detail: 'Result envelope returned; lineage queryable forever' },
];

export default function ProjectsPage() {
  return (
    <main>
      <section className="page-hero">
        <div className="container">
          <p className="eyebrow">Artifacts &amp; jobs</p>
          <h1>
            Every output carries <span className="accent-word">its own receipt.</span>
          </h1>
          <p className="lede">
            Files live on the filesystem; only metadata goes into SQLite — artifact ID, type, size
            and SHA-256 checksum, tied to the job that produced them. CAD results are never cached
            or reused: provenance beats performance.
          </p>
        </div>
      </section>

      <section className="section-sm" aria-label="Artifact formats">
        <div className="container">
          <div className="showcase-grid">
            {artifacts.map((artifact, i) => (
              <Reveal key={artifact.type} delay={i * 80}>
                <article className="showcase-card">
                  <div className="telemetry">
                    <span>ARTIFACT / {artifact.type}</span>
                    <span className="status-pill is-live">{artifact.tag}</span>
                  </div>
                  <h3>{artifact.name}</h3>
                  <p>{artifact.body}</p>
                </article>
              </Reveal>
            ))}
          </div>
        </div>
      </section>

      <section className="section" aria-labelledby="flagship-title">
        <div className="container">
          <div className="section-head">
            <Reveal>
              <div>
                <p className="eyebrow">Flagship request</p>
                <h2 id="flagship-title">The 50 mm quadcopter frame.</h2>
              </div>
            </Reveal>
            <Reveal>
              <p className="lede">
                One POST produces a validated frame plus three artifacts. The response envelope
                carries the spec, triangle count, bounding box and every check that passed.
              </p>
            </Reveal>
          </div>

          <Reveal>
            <div className="compare">
              <div className="compare-row compare-head">
                <div>Field</div>
                <div>Value</div>
                <div>Notes</div>
              </div>
              {[
                ['overall_size', '50 mm', 'Motor-to-motor diagonal, X configuration'],
                ['arm_width · plate_thickness', '5 · 1.5 mm', 'FDM manufacturability floor: ≥ 1 mm'],
                ['validation', 'VALIDATED', 'dimensions · topology · clearances · manufacturability'],
                ['artifacts', 'STL + GLB + JSON', 'Checksummed, job-linked, downloadable'],
                ['unavailable_formats', 'STEP → 501', 'CAD_KERNEL_UNAVAILABLE, reported honestly'],
              ].map(([field, value, notes]) => (
                <div className="compare-row" key={field}>
                  <div style={{ color: 'var(--text)' }}>{field}</div>
                  <div className="yes">{value}</div>
                  <div className="partial">{notes}</div>
                </div>
              ))}
            </div>
          </Reveal>
        </div>
      </section>

      <section className="section-sm" aria-labelledby="lifecycle-title">
        <div className="container">
          <div className="section-head">
            <Reveal>
              <div>
                <p className="eyebrow">Job lifecycle</p>
                <h2 id="lifecycle-title">Queued → running → completed.</h2>
              </div>
            </Reveal>
            <Reveal>
              <p className="lede">
                Engine-time failures come back as tracked failed jobs — HTTP 200 with success:false
                and classified errors. Lookups that miss return real 404s.
              </p>
            </Reveal>
          </div>

          <div className="process-grid">
            {lifecycle.map((step, i) => (
              <Reveal key={step.n} delay={i * 70}>
                <article className="process-step">
                  <div className="step-num">
                    <span>{step.n}</span>
                    <span>{step.name}</span>
                  </div>
                  <p>{step.detail}</p>
                </article>
              </Reveal>
            ))}
          </div>

          <Reveal>
            <div style={{ marginTop: 40, display: 'flex', gap: 12, flexWrap: 'wrap' }}>
              <Link className="btn btn-primary" href="/#workspace">
                Generate one now <span aria-hidden="true">↗</span>
              </Link>
              <Link className="btn btn-ghost" href="/services">
                Browse engines
              </Link>
            </div>
          </Reveal>
        </div>
      </section>
    </main>
  );
}

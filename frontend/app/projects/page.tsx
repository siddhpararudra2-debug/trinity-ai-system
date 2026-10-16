import type { Metadata } from 'next';
import Link from 'next/link';
import Reveal from '@/components/sections/Reveal';

/**
 * Artifacts and the part catalog, both read from the running backend rather
 * than written down here. The catalog fetch is server-side and degrades to
 * an honest notice instead of a placeholder list — this page used to carry
 * hardcoded project cards (a titanium hub, a gimbal, an avionics box) that
 * no part of the system could actually produce.
 */

export const metadata: Metadata = {
  title: 'Artifacts & Jobs',
  description:
    'Trinity AI artifacts: checksummed STL, GLB, JSON and STEP outputs with job provenance, validation records and SQLite lineage.',
};

const API = process.env.NEXT_PUBLIC_TRINITY_API_URL ?? 'http://localhost:8000/api';

type CatalogPart = {
  name: string;
  title?: string;
  description?: string;
  parameters?: Array<{ name: string; unit?: string; default?: number | string }>;
};

type Catalog = {
  parts?: CatalogPart[];
  default_outputs?: string[];
  outputs?: Record<string, { available: boolean; reason?: string }>;
};

const artifactFormats = [
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
  {
    type: 'STEP',
    name: 'AP214 faceted B-rep',
    body: 'Optional — needs an OpenCascade kernel. The mesh is sewn into planar faces and one solid per connected body. Faceted, not analytic: a cylinder arrives as a polygonal prism.',
    tag: 'KERNEL',
  },
];

const lifecycle = [
  { n: '01', name: 'QUEUED', detail: 'Request validated at the Pydantic edge, job row inserted' },
  { n: '02', name: 'RUNNING', detail: 'Engine executes off the event loop in scratch space' },
  { n: '03', name: 'VALIDATED', detail: 'Independent checks recorded in the validations table' },
  { n: '04', name: 'STORED', detail: 'Artifacts copied into managed storage with SHA-256 checksums' },
  { n: '05', name: 'COMPLETED', detail: 'Result envelope returned; lineage queryable forever' },
];

async function loadCatalog(): Promise<Catalog | null> {
  try {
    const response = await fetch(`${API}/cad/catalog`, { cache: 'no-store' });
    if (!response.ok) return null;
    return (await response.json()) as Catalog;
  } catch {
    return null;
  }
}

export default async function ProjectsPage() {
  const catalog = await loadCatalog();
  const parts = catalog?.parts ?? [];
  const step = catalog?.outputs?.step;

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
            {artifactFormats.map((artifact, i) => (
              <Reveal key={artifact.type} delay={i * 80}>
                <article className="showcase-card">
                  <div className="telemetry">
                    <span>ARTIFACT / {artifact.type}</span>
                    <span className={`status-pill ${artifact.type === 'STEP' && step?.available === false ? 'is-scaffold' : 'is-live'}`}>
                      {artifact.type === 'STEP'
                        ? step?.available === false
                          ? 'NO KERNEL'
                          : 'AVAILABLE'
                        : artifact.tag}
                    </span>
                  </div>
                  <h3>{artifact.name}</h3>
                  <p>{artifact.body}</p>
                </article>
              </Reveal>
            ))}
          </div>
          {step && step.available === false && (
            <p className="catalog-status" style={{ marginTop: 16 }}>
              STEP is unavailable on this backend: {step.reason}
            </p>
          )}
        </div>
      </section>

      <section className="section" aria-labelledby="catalog-title">
        <div className="container">
          <div className="section-head">
            <Reveal>
              <div>
                <p className="eyebrow">Live catalog</p>
                <h2 id="catalog-title">The parts this build can actually make.</h2>
              </div>
            </Reveal>
            <Reveal>
              <p className="lede">
                Read straight from <span className="mono">GET /api/cad/catalog</span>. Every entry
                below has a builder, a validator and a filename scheme behind it.
              </p>
            </Reveal>
          </div>

          {parts.length > 0 ? (
            <div className="showcase-grid">
              {parts.map((part, i) => (
                <Reveal key={part.name} delay={i * 70}>
                  <article className="showcase-card">
                    <div className="telemetry">
                      <span>{part.name.toUpperCase()}</span>
                      <span className="status-pill is-pass">
                        {part.parameters?.length ?? 0} PARAMS
                      </span>
                    </div>
                    <h3>{part.title ?? part.name}</h3>
                    <p>{part.description ?? 'No description published for this part.'}</p>
                  </article>
                </Reveal>
              ))}
            </div>
          ) : (
            <p className="lede">
              The catalog could not be read from {API}. Start the backend and reload — this page
              will not invent a part list.
            </p>
          )}
        </div>
      </section>

      <section className="section-sm" aria-labelledby="flagship-title">
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
                One POST produces a validated frame plus its artifacts. The response envelope
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
                [
                  'artifacts',
                  catalog?.default_outputs?.map((o) => o.toUpperCase()).join(' + ') ?? 'STL + JSON',
                  'Checksummed, job-linked, downloadable',
                ],
                [
                  'step',
                  step?.available ? 'WRITTEN' : 'REPORTED UNAVAILABLE',
                  step?.available
                    ? 'Faceted AP214 B-rep written through OpenCascade'
                    : 'Named under result.unavailable_formats with the reason — never silently dropped',
                ],
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
            {lifecycle.map((step_) => (
              <Reveal key={step_.n} delay={Number(step_.n) * 70}>
                <article className="process-step">
                  <div className="step-num">
                    <span>{step_.n}</span>
                    <span>{step_.name}</span>
                  </div>
                  <p>{step_.detail}</p>
                </article>
              </Reveal>
            ))}
          </div>

          <Reveal>
            <div style={{ marginTop: 40, display: 'flex', gap: 12, flexWrap: 'wrap' }}>
              <Link className="btn btn-primary" href="/">
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
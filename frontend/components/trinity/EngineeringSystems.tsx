'use client';

import SystemDiagram from './SystemDiagram';

const SYSTEMS = [
  {
    n: '01',
    title: 'CAD Generation',
    desc: 'Natural language → parametric geometry through a deterministic IR and native mesh kernel.',
    meta: ['DETERMINISTIC', 'STL · GLB · JSON'],
    inputs: ['PARAMS', 'CAD IR', 'MESH'],
    outputs: ['STL', 'GLB', 'JSON'],
    center: ['CAD', 'TRINITY / 01'] as [string, string],
  },
  {
    n: '02',
    title: 'Requirement Analysis',
    desc: 'Plain requirements mapped onto structured, Pydantic-validated requests — no model in the loop.',
    meta: ['REGEX ROUTER', 'TYPED IR'],
    inputs: ['TEXT', 'REGEX', 'PARSER'],
    outputs: ['IR', 'PARAMS', 'VALIDATED'],
    center: ['PARSER', 'TRINITY / 02'] as [string, string],
  },
  {
    n: '03',
    title: 'Geometry Validation',
    desc: 'Geometry → dimensional and topology checks. Independent of generation — GENERATED never becomes artifact alone.',
    meta: ['INDEPENDENT', '4 CHECKS'],
    inputs: ['MESH', 'BOUNDS', 'TOPO'],
    outputs: ['PASS', 'WARN', 'FAIL'],
    center: ['VALIDATOR', 'TRINITY / 03'] as [string, string],
  },
  {
    n: '04',
    title: 'Simulation',
    desc: 'Engineering parameters → simulation workflows. DAG primitives already order multi-step runs.',
    meta: ['DAG READY', 'SCAFFOLD'],
    inputs: ['PARAMS', 'LOADS', 'MESH'],
    outputs: ['RESULTS', 'FIELDS', 'REPORT'],
    center: ['SIM', 'TRINITY / 04'] as [string, string],
  },
  {
    n: '05',
    title: 'Manufacturing',
    desc: 'Geometry → manufacturing-ready artifacts. SHA-256, job lineage, no silent fallbacks — STEP returns 501 until kernel configured.',
    meta: ['PROVENANCE', '501 HONEST'],
    inputs: ['GEOMETRY', 'TOLERANCES', 'MATERIAL'],
    outputs: ['G-CODE', 'STEP*', 'QC'],
    center: ['MFG', 'TRINITY / 05'] as [string, string],
  },
  {
    n: '06',
    title: 'Engineering Memory',
    desc: 'Projects → reusable engineering knowledge. Jobs, artifacts and validations in SQLite — metadata-only lineage.',
    meta: ['SQLITE WAL', 'LINEAGE'],
    inputs: ['JOBS', 'ARTIFACTS', 'CHECKS'],
    outputs: ['HISTORY', 'REUSE', 'TRACE'],
    center: ['MEMORY', 'TRINITY / 06'] as [string, string],
  },
];

export default function EngineeringSystems() {
  return (
    <section className="systems-section" id="systems" aria-labelledby="systems-title">
      <div className="container">
        <div className="section-label">06 / ENGINEERING SYSTEMS</div>
        <div className="systems-head">
          <div>
            <h2 id="systems-title">One intelligence layer. Multiple engineering systems.</h2>
          </div>
          <p className="lede">
            Six registered systems. Two live, four truthful scaffolds that raise
            capability errors rather than faking output.
          </p>
        </div>

        <div className="systems-grid">
          {SYSTEMS.map((s) => (
            <article key={s.n} className="system-card">
              <div className="system-card-num">SYSTEM {s.n}</div>
              <h3>{s.title}</h3>
              <p>{s.desc}</p>
              <div className="system-card-meta">
                {s.meta.map((m) => (
                  <span key={m} className="status-pill" style={{ fontSize: '0.56rem' }}>
                    {m}
                  </span>
                ))}
              </div>
              <SystemDiagram inputs={s.inputs} outputs={s.outputs} center={s.center} />
            </article>
          ))}
        </div>
      </div>
    </section>
  );
}

import type { Metadata } from 'next';
import Reveal from '@/components/sections/Reveal';

export const metadata: Metadata = {
  title: 'Engines',
  description:
    'The Trinity engine registry: a live CAD engine and math engine, plus truthful scaffolds for PCB, firmware, vision, research, simulation and robotics.',
};

const engines = [
  {
    id: 'cad',
    name: 'CAD',
    status: 'LIVE',
    version: 'v1.0',
    capabilities: ['generate', 'validate', 'export', 'preview'],
    body: 'Parametric quadcopter-frame generation through a library-independent IR, a deterministic native mesh builder and an independent validator. Binary STL and glTF 2.0 GLB exporters ship in the core; STEP is an explicit capability limit until CadQuery/OpenCascade is configured.',
    facts: ['QuadcopterFrameIR · X configuration', 'Binary STL · GLB · JSON', '4 independent geometry checks'],
  },
  {
    id: 'math',
    name: 'MATH',
    status: 'LIVE',
    version: 'v1.0',
    capabilities: ['solve', 'evaluate'],
    body: 'SymPy-backed symbolic solving and numeric evaluation. Roots are re-substituted into the original expression and verified to numerical precision before the result is marked VALIDATED — the "20% of 50, no LLM required" example, end to end.',
    facts: ['Deterministic SymPy core', 'Residual check < 1e-9', 'Identity + multi-symbol solve'],
  },
  {
    id: 'pcb',
    name: 'PCB',
    status: 'SCAFFOLD',
    version: 'registered',
    capabilities: ['inspect', 'validate', 'generate', 'export'],
    body: 'Registered with real capabilities. Raises CapabilityUnavailableError instead of producing placeholder boards — a KiCad-backed implementation slots in behind the same interface.',
    facts: ['Truthful 501 responses', 'No fake artifacts'],
  },
  {
    id: 'firmware',
    name: 'FIRMWARE',
    status: 'SCAFFOLD',
    version: 'registered',
    capabilities: ['create', 'build', 'test', 'compile'],
    body: 'Generated firmware code will only ever run in a restricted subprocess. Until that sandbox exists, the scaffold refuses.',
    facts: ['Sandbox-first design', 'No code execution yet'],
  },
  {
    id: 'vision',
    name: 'VISION',
    status: 'SCAFFOLD',
    version: 'registered',
    capabilities: ['image_inspect', 'ocr', 'document_parse', 'geometry_extract'],
    body: 'Document and geometry extraction slot for the intelligence boundary — deterministic adapters first, models later, never instead.',
    facts: ['Adapter-shaped', 'Registry-discoverable'],
  },
  {
    id: 'research',
    name: 'RESEARCH',
    status: 'SCAFFOLD',
    version: 'registered',
    capabilities: ['search'],
    body: 'A minimal search capability stub. Structured, cacheable and provenance-preserving when implemented.',
    facts: ['Capability stub'],
  },
  {
    id: 'simulation',
    name: 'SIMULATION',
    status: 'SCAFFOLD',
    version: 'registered',
    capabilities: ['simulate'],
    body: 'Simulation hook for the DAG workflow primitives — the ordered() topology already supports dependency-graph execution.',
    facts: ['DAG-ready'],
  },
  {
    id: 'robotics',
    name: 'ROBOTICS',
    status: 'SCAFFOLD',
    version: 'registered',
    capabilities: ['kinematics', 'trajectory_plan'],
    body: 'Kinematics and trajectory planning reserved behind the same four-method engine interface.',
    facts: ['Same engine contract'],
  },
];

export default function ServicesPage() {
  return (
    <main>
      <section className="page-hero">
        <div className="container">
          <p className="eyebrow">Engine registry</p>
          <h1>
            What Trinity <span className="accent-word">can do</span> — and what it refuses to fake.
          </h1>
          <p className="lede">
            GET /api/engines returns every registered capability as structured data. Two engines are
            live; six are honest scaffolds that raise capability errors rather than pretending.
          </p>
        </div>
      </section>

      <section className="section-sm" aria-label="Engine details">
        <div className="container">
          <div className="workspace-grid">
            {engines.map((engine, i) => (
              <Reveal key={engine.id} delay={(i % 2) * 70}>
                <article className="ws-panel" id={engine.id} style={{ height: '100%' }}>
                  <h2>
                    {engine.name} · {engine.version}
                  </h2>
                  <p className="ws-empty">{engine.body}</p>
                  <ul className="ws-checks" style={{ marginTop: 16 }}>
                    {engine.facts.map((fact) => (
                      <li key={fact}>
                        <b>▸</b> {fact}
                      </li>
                    ))}
                  </ul>
                  <div style={{ display: 'flex', gap: 8, flexWrap: 'wrap', marginTop: 18 }}>
                    <span className={`status-pill ${engine.status === 'LIVE' ? 'is-live' : 'is-scaffold'}`}>
                      {engine.status}
                    </span>
                    {engine.capabilities.map((cap) => (
                      <span className="status-pill" key={cap}>
                        {cap}
                      </span>
                    ))}
                  </div>
                </article>
              </Reveal>
            ))}
          </div>
        </div>
      </section>

      <section className="section-sm" aria-labelledby="adapter-title">
        <div className="container">
          <div className="section-head">
            <Reveal>
              <div>
                <p className="eyebrow">Extension points</p>
                <h2 id="adapter-title">Adapters, not rewrites.</h2>
              </div>
            </Reveal>
            <Reveal>
              <p className="lede">
                CadQuery, FreeCAD and Onshape adapters satisfy the same four methods — nothing above
                the engine layer changes when a real kernel drops in.
              </p>
            </Reveal>
          </div>

          <Reveal>
            <div className="code-block">
              <code>
                <span className="cmt"># POST /api/cad/generate — the flagship request</span>
                {'\n'}curl -X POST localhost:8000/api/cad/generate \
                  -H &apos;Content-Type: application/json&apos; \
                  -d &apos;{'{'}&quot;parameters&quot;: {'{'}&quot;overall_size&quot;: 50{'}'},
                &quot;outputs&quot;: [&quot;stl&quot;, &quot;glb&quot;, &quot;json&quot;]{'}'}&apos;
              </code>
            </div>
          </Reveal>
        </div>
      </section>
    </main>
  );
}

'use client';

import { useState } from 'react';

const DIAGRAM_PATHS = [
  'M118 105 C260 105 265 235 340 250',
  'M118 205 C245 205 275 240 340 250',
  'M118 315 C245 315 275 260 340 250',
  'M118 415 C260 415 265 270 340 250',
  'M430 250 C505 250 515 130 610 130',
  'M430 250 C520 250 520 250 610 250',
  'M430 250 C505 250 515 370 610 370',
];

type EngineTab = {
  id: string;
  tab: string;
  label: string;
  count: string;
  title: string;
  body: string;
  steps: { n: string; name: string; detail: string }[];
  sources: string[];
  outputs: string[];
  core: [string, string];
};

const TABS: EngineTab[] = [
  {
    id: 'cad',
    tab: 'CAD engine',
    label: 'FROM REQUIREMENT → TO VALIDATED GEOMETRY',
    count: '01 / CAD',
    title: 'Parametric quadcopter frame generation',
    body: 'A library-independent IR feeds a deterministic native mesh builder. An independent validator checks dimensions, topology, clearances and manufacturability before any artifact is written. STEP is an explicit capability limit until a CadQuery/OpenCascade kernel is configured.',
    steps: [
      { n: '01', name: 'Requirement', detail: 'Deterministic NL router' },
      { n: '02', name: 'CAD IR', detail: 'Backend-agnostic parameters' },
      { n: '03', name: 'Geometry', detail: 'Native mesh builder' },
      { n: '04', name: 'Validation', detail: 'Four independent checks' },
      { n: '05', name: 'Artifacts', detail: 'STL · GLB · JSON' },
    ],
    sources: ['PARAMETERS', 'CAD IR', 'MESH BUILDER', 'VALIDATOR'],
    outputs: ['BINARY STL', 'GLTF 2.0 GLB', 'JSON SPEC'],
    core: ['CAD\nENGINE', 'TRINITY / 01'],
  },
  {
    id: 'math',
    tab: 'Math engine',
    label: 'FROM EXPRESSION → TO VERIFIED SOLUTION',
    count: '02 / MATH',
    title: 'Symbolic solving with residual checks',
    body: 'SymPy-backed solve and evaluate. Every root is re-substituted into the original expression and confirmed to numerical precision before the job is marked VALIDATED — the simplest proof of the engine → validate → result pipeline, with no model in the loop.',
    steps: [
      { n: '01', name: 'Expression', detail: 'Structured request' },
      { n: '02', name: 'Sympify', detail: 'Typed symbols, no collisions' },
      { n: '03', name: 'Solve', detail: 'SymPy deterministic core' },
      { n: '04', name: 'Residual', detail: 'Roots re-checked < 1e-9' },
      { n: '05', name: 'Verdict', detail: 'VALIDATED / FAILED' },
    ],
    sources: ['EXPRESSION', 'VARIABLES', 'SOLVE_FOR', 'UNITS'],
    outputs: ['SOLUTIONS', 'RESIDUAL CHECKS', 'VALIDATION'],
    core: ['MATH\nENGINE', 'TRINITY / 02'],
  },
  {
    id: 'jobs',
    tab: 'Jobs & lineage',
    label: 'FROM API CALL → TO TRACEABLE WORK',
    count: '03 / SYSTEM',
    title: 'Every execution is a tracked job',
    body: 'The job manager runs each engine call off the event loop and records queued → running → completed | failed in SQLite. Artifacts are checksummed into managed storage; pure deterministic operations hit a SQLite cache. No broker, no queue — provenance instead.',
    steps: [
      { n: '01', name: 'Request', detail: 'Pydantic edge validation' },
      { n: '02', name: 'Job', detail: 'SQLite-tracked state' },
      { n: '03', name: 'Engine', detail: 'Registry dispatch' },
      { n: '04', name: 'Store', detail: 'SHA-256 artifact manager' },
      { n: '05', name: 'Lineage', detail: 'Validations + job history' },
    ],
    sources: ['API REQUEST', 'REGISTRY', 'CACHE', 'SCRATCH DIR'],
    outputs: ['JOB RECORD', 'ARTIFACT REFS', 'VALIDATION ROW'],
    core: ['JOB\nMANAGER', 'TRINITY / 03'],
  },
  {
    id: 'scaffolds',
    tab: 'Truthful scaffolds',
    label: 'FROM ROADMAP → TO HONEST LIMITS',
    count: '04 / NEXT',
    title: 'Registered capabilities that refuse to fake it',
    body: 'PCB, firmware, vision, research, simulation and robotics are registered in the engine registry with real capability lists — and raise CapabilityUnavailableError instead of pretending. Scaffolds become engines without touching the API layer.',
    steps: [
      { n: '01', name: 'PCB', detail: 'inspect · validate · generate' },
      { n: '02', name: 'Firmware', detail: 'create · build · test' },
      { n: '03', name: 'Vision', detail: 'ocr · document_parse' },
      { n: '04', name: 'Research', detail: 'search' },
      { n: '05', name: 'Simulation', detail: 'simulate' },
    ],
    sources: ['REGISTRY', 'CAPABILITIES', 'ADAPTERS', 'ERRORS'],
    outputs: ['501 RESPONSES', 'NO FAKE FILES', 'DROP-IN SLOTS'],
    core: ['SCAFFOLD\nENGINES', 'TRINITY / 04'],
  },
];

export default function EngineConsole() {
  const [active, setActive] = useState(0);
  const tab = TABS[active];

  return (
    <div className="console">
      <div className="console-tabs" role="tablist" aria-label="Trinity systems">
        {TABS.map((t, i) => (
          <button
            key={t.id}
            className={`console-tab ${i === active ? 'is-active' : ''}`}
            role="tab"
            aria-selected={i === active}
            onClick={() => setActive(i)}
            type="button"
          >
            {t.tab}
          </button>
        ))}
      </div>

      <div className="console-panel" role="tabpanel">
        <div className="console-panel-copy">
          <span className="console-label">{tab.label}</span>
          <p className="console-count">{tab.count}</p>
          <h3>{tab.title}</h3>
          <p>{tab.body}</p>
          <ol className="system-steps">
            {tab.steps.map((s) => (
              <li key={s.n}>
                <span>{s.n}</span>
                <strong>{s.name}</strong>
                <small>{s.detail}</small>
              </li>
            ))}
          </ol>
        </div>

        <div className="system-diagram" aria-label={`${tab.tab} system diagram`}>
          <div className="diagram-source source-a">{tab.sources[0]}</div>
          <div className="diagram-source source-b">{tab.sources[1]}</div>
          <div className="diagram-source source-c">{tab.sources[2]}</div>
          <div className="diagram-source source-d">{tab.sources[3]}</div>
          <div className="diagram-core">
            <i />
            <strong>
              {tab.core[0].split('\n')[0]}
              <br />
              {tab.core[0].split('\n')[1]}
            </strong>
            <small>{tab.core[1]}</small>
          </div>
          <div className="diagram-output output-a">{tab.outputs[0]}</div>
          <div className="diagram-output output-b">{tab.outputs[1]}</div>
          <div className="diagram-output output-c">{tab.outputs[2]}</div>
          <svg className="diagram-lines" viewBox="0 0 720 520" preserveAspectRatio="none" aria-hidden="true">
            {DIAGRAM_PATHS.map((d) => (
              <path key={d} d={d} />
            ))}
          </svg>
        </div>
      </div>
    </div>
  );
}

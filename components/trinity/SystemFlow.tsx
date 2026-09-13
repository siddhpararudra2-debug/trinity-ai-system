/* eslint-disable react-hooks/set-state-in-effect */
'use client';

import { useEffect, useState } from 'react';

const NODES = [
  { id: 'intent', label: 'USER INTENT', sub: 'Natural language', dot: '○' },
  { id: 'requirements', label: 'REQUIREMENTS', sub: 'Structured request', dot: '◐' },
  { id: 'parameters', label: 'PARAMETERS', sub: 'CAD IR · overall_size', dot: '◑' },
  { id: 'geometry', label: 'GEOMETRY', sub: 'Native mesh builder', dot: '◒' },
  { id: 'validation', label: 'VALIDATION', sub: '4 independent checks', dot: '◓' },
  { id: 'artifact', label: 'ARTIFACT', sub: 'STL · GLB · JSON', dot: '●' },
] as const;

export default function SystemFlow() {
  const [active, setActive] = useState(2);
  const [reduced, setReduced] = useState(false);

  useEffect(() => {
    setReduced(window.matchMedia('(prefers-reduced-motion: reduce)').matches);
    const mq = window.matchMedia('(prefers-reduced-motion: reduce)');
    const onChange = (e: MediaQueryListEvent) => setReduced(e.matches);
    mq.addEventListener?.('change', onChange);
    return () => mq.removeEventListener?.('change', onChange);
  }, []);

  useEffect(() => {
    if (reduced) return;
    const id = setInterval(() => setActive((a) => (a + 1) % NODES.length), 2200);
    return () => clearInterval(id);
  }, [reduced]);

  const onKey = (e: React.KeyboardEvent, idx: number) => {
    if (e.key === 'Enter' || e.key === ' ') {
      e.preventDefault();
      setActive(idx);
    }
    if (e.key === 'ArrowRight') {
      e.preventDefault();
      setActive((a) => Math.min(NODES.length - 1, a + 1));
    }
    if (e.key === 'ArrowLeft') {
      e.preventDefault();
      setActive((a) => Math.max(0, a - 1));
    }
  };

  return (
    <section className="flow-section" id="system-flow" aria-labelledby="flow-title">
      <div className="container">
        <div className="section-label">02 / SYSTEM FLOW</div>
        <div className="flow-head">
          <div>
            <h2 id="flow-title">From intent to geometry.</h2>
          </div>
          <p className="lede">
            Requirement → IR → geometry → validation → artifact. Each node is inspectable; hover to reveal its contract.
          </p>
        </div>

        <div className="flow-diagram" role="list" aria-label="Trinity engineering pipeline">
          {NODES.map((n, i) => (
            <div
              key={n.id}
              className={`flow-node ${i === active ? 'is-active' : i < active ? 'is-done' : ''}`}
              role="listitem"
              tabIndex={0}
              onMouseEnter={() => setActive(i)}
              onFocus={() => setActive(i)}
              onKeyDown={(e) => onKey(e, i)}
              aria-current={i === active ? 'step' : undefined}
            >
              <span>{String(i + 1).padStart(2, '0')}</span>
              <div className="node-dot" aria-hidden="true" />
              <strong>{n.label}</strong>
              <small>{n.sub}</small>
              {i < NODES.length - 1 && <span className="flow-connector" aria-hidden="true" />}
            </div>
          ))}
        </div>

        <div className="flow-panel-detail" aria-live="polite">
          <div>
            <strong style={{ color: '#111111', fontSize: '0.76rem', letterSpacing: '0.06em', textTransform: 'uppercase' }}>
              {NODES[active].label}
            </strong>
            <div style={{ marginTop: 8, lineHeight: 1.6 }}>
              {active === 0 && 'Plain-language requirement. The V1 deterministic router matches “Create a 50 mm quadcopter frame” onto structured parameters — no model, no prompt.'}
              {active === 1 && 'Pydantic-validated request. Invalid requirements raise a 422 with a classified error — never a silent fallback.'}
              {active === 2 && 'Backend-agnostic IR: overall_size, arm dimensions, plate thickness. The same IR could feed CadQuery or FreeCAD adapters.'}
              {active === 3 && 'Pure-Python mesh kernel: center plate, X-configuration arms, motor bosses. Binary STL + glTF 2.0 writers, no third-party mesh deps.'}
              {active === 4 && 'Independent validator: dimensions, topology, clearances, FDM manufacturability. GENERATED → VALIDATED only if all pass.'}
              {active === 5 && 'Checksummed artifacts in managed storage with SHA-256 and SQLite lineage. CAD generation is never cached — provenance beats performance.'}
            </div>
          </div>
          <div>
            <div style={{ fontSize: '0.62rem', letterSpacing: '0.1em', textTransform: 'uppercase', color: '#5A5A56', marginBottom: 8 }}>
              CONTRACT
            </div>
            <code style={{ display: 'block', padding: 12, background: '#FFFFFF', border: '1px solid #D0D0CA', fontSize: '0.72rem', lineHeight: 1.6 }}>
              {active === 0 && 'POST /api/requirements/execute\n{ "text": "Create a 50 mm quadcopter frame" }'}
              {active === 1 && 'RequirementRequest\n{ text: string (1–1000) }'}
              {active === 2 && 'CADGenerateRequest\n{ type: "quadcopter_frame", parameters: { overall_size: 50 } }'}
              {active === 3 && 'EngineResult\n{ triangle_count: number, spec: { parameters } }'}
              {active === 4 && 'ValidationOut\n{ status: "VALIDATED", checks: { dimensions, topology… } }'}
              {active === 5 && 'ToolResponse\n{ success, artifacts: [{ type, size_bytes, checksum }], job_id }'}
            </code>
          </div>
        </div>
      </div>
    </section>
  );
}

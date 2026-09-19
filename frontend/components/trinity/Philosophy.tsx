export default function Philosophy() {
  return (
    <section className="philosophy-section" id="about" aria-labelledby="philosophy-title">
      <div className="container">
        <div className="section-label">08 / TRINITY</div>
        <div className="philosophy-grid">
          <div>
            <h2 id="philosophy-title">Engineering intelligence should understand what you&apos;re building.</h2>
            <p className="lede" style={{ marginTop: 18, maxWidth: 560 }}>
              Trinity is not another AI chatbot. It is an engineering intelligence
              system — models may reason, but deterministic tools execute and
              independent checks verify. Every file has a verdict and a lineage.
            </p>
            <div style={{ marginTop: 32, display: 'grid', gap: 16, maxWidth: 560 }}>
              <div style={{ display: 'flex', gap: 16, paddingBottom: 16, borderBottom: '1px solid #D0D0CA' }}>
                <span style={{ fontFamily: 'var(--font-mono-plex), monospace', fontSize: '0.7rem', color: '#315B73', fontWeight: 600 }}>01</span>
                <p style={{ margin: 0, fontSize: '0.9rem', color: '#707070', lineHeight: 1.6 }}>
                  <strong style={{ color: '#111111' }}>Models reason, tools execute.</strong> A future model provider can emit structured tool calls — it never runs engines itself. Execution stays deterministic and inspectable.
                </p>
              </div>
              <div style={{ display: 'flex', gap: 16, paddingBottom: 16, borderBottom: '1px solid #D0D0CA' }}>
                <span style={{ fontFamily: 'var(--font-mono-plex), monospace', fontSize: '0.7rem', color: '#315B73', fontWeight: 600 }}>02</span>
                <p style={{ margin: 0, fontSize: '0.9rem', color: '#707070', lineHeight: 1.6 }}>
                  <strong style={{ color: '#111111' }}>Validation is independent.</strong> Generation and verification are separate steps. A mesh that fails checks never becomes an artifact.
                </p>
              </div>
              <div style={{ display: 'flex', gap: 16 }}>
                <span style={{ fontFamily: 'var(--font-mono-plex), monospace', fontSize: '0.7rem', color: '#315B73', fontWeight: 600 }}>03</span>
                <p style={{ margin: 0, fontSize: '0.9rem', color: '#707070', lineHeight: 1.6 }}>
                  <strong style={{ color: '#111111' }}>Every artifact has provenance.</strong> Server-generated IDs, SHA-256 checksums and SQLite job lineage behind every file.
                </p>
              </div>
            </div>
          </div>

          <div style={{ display: 'grid', gap: 16 }}>
            <div className="philosophy-meta">
              <h4>CURRENT FUNCTIONALITY</h4>
              <p>
                Requirement → CAD IR → native mesh builder → independent validation → STL / GLB / JSON artifacts with checksums. Math engine (SymPy solve with residual checks) and truthful scaffolds for PCB, firmware, vision, research, simulation, robotics.
              </p>
              <div style={{ marginTop: 12, display: 'flex', gap: 8, flexWrap: 'wrap' }}>
                <span className="status-pill is-live">LIVE</span>
                <span className="status-pill">POST /api/requirements/execute</span>
                <span className="status-pill">POST /api/cad/generate</span>
              </div>
            </div>
            <div className="philosophy-meta" style={{ background: '#F1F1ED', borderStyle: 'dashed' }}>
              <h4>PLANNED — HONESTLY SCAFFOLDED</h4>
              <p>
                CadQuery/OpenCascade kernel (STEP export), KiCad PCB, firmware sandbox, vision adapters, simulation DAG pipelines, model provider boundary. Scaffolds return 501 capability errors today — never fake files.
              </p>
              <div style={{ marginTop: 12, display: 'flex', gap: 8, flexWrap: 'wrap' }}>
                <span className="status-pill is-scaffold">SCAFFOLD</span>
                <span className="status-pill">STEP → 501</span>
                <span className="status-pill">NO FAKE OUTPUT</span>
              </div>
            </div>
            <div style={{ padding: 18, border: '1px solid #111111', background: '#111111', color: '#F7F7F4' }}>
              <div style={{ fontFamily: 'var(--font-mono-plex), monospace', fontSize: '0.62rem', letterSpacing: '0.12em', opacity: 0.6, marginBottom: 8 }}>
                TRINITY / 2026 — BUILT FOR ENGINEERS
              </div>
              <div style={{ fontFamily: 'var(--font-display), sans-serif', fontSize: '1.2rem', letterSpacing: '-0.03em', lineHeight: 1.2 }}>
                Deterministic tools execute.
                <br />
                Independent checks verify.
                <br />
                <span style={{ color: '#6D8998' }}>Models may reason later.</span>
              </div>
            </div>
          </div>
        </div>
      </div>
    </section>
  );
}

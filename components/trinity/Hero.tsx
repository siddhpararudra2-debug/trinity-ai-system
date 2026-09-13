'use client';

import Link from 'next/link';
import dynamic from 'next/dynamic';

const HeroScene = dynamic(() => import('./HeroScene'), {
  ssr: false,
  loading: () => (
    <div
      style={{
        width: '100%',
        height: '100%',
        minHeight: 520,
        background: '#F1F1ED',
        display: 'grid',
        placeItems: 'center',
        fontFamily: 'var(--font-mono-plex), monospace',
        fontSize: '0.68rem',
        letterSpacing: '0.12em',
        color: '#707070',
      }}
    >
      INITIALIZING GEOMETRY —
    </div>
  ),
});

export default function Hero() {
  return (
    <section className="hero" id="top" aria-label="Hero">
      <div className="container hero-shell">
        <div className="hero-copy">
          <div className="hero-meta">
            <p className="eyebrow" style={{ margin: 0 }}>
              TRINITY / ENGINEERING INTELLIGENCE — V1 OPERATING SYSTEM
            </p>
          </div>

          <h1>
            Design.
            <br />
            Simulate.
            <br />
            <span className="accent-word">Validate.</span>
            <br />
            <span style={{ fontWeight: 400, color: '#5A5A56' }}>With intelligence.</span>
          </h1>

          <div className="hero-copy-bottom">
            <p className="lede">
              Turn natural-language engineering requirements into structured
              specifications, geometry, validation data and production-ready
              artifacts — deterministically.
            </p>
            <div>
              <div className="hero-actions">
                <Link href="#workspace" className="btn btn-primary">
                  START ENGINEERING <span aria-hidden="true">→</span>
                </Link>
                <Link href="#systems" className="text-link" style={{ marginTop: 0, padding: '12px 0' }}>
                  Explore system <span aria-hidden="true">→</span>
                </Link>
              </div>
              <div className="proof-line">
                <span className="proof-dot" aria-hidden="true" />
                <span>
                  V1 flagship: validated 50 mm quadcopter frame — checksummed
                  STL, GLB and JSON.
                </span>
              </div>
            </div>
          </div>
        </div>

        <div
          className="stage"
          role="img"
          aria-label="Interactive 3D engineering visualization of 50 mm quadcopter frame with dimension annotations"
        >
          <HeroScene />
          <div className="stage-top">
            <span>TRINITY / GEOMETRY ENGINE — 50.00 MM</span>
            <span className="stage-live">
              <i aria-hidden="true" /> LIVE
            </span>
          </div>
          <div className="stage-center-copy" aria-hidden="true">
            <strong>
              DETERMINISTIC
              <br />
              CORE
            </strong>
            <small>requirement → execution → validation → artifact</small>
          </div>
          <div className="stage-legend" aria-hidden="true">
            <span>
              <i /> PLATE
            </span>
            <span>
              <i /> ARMS
            </span>
            <span className="legend-violet">
              <i /> MOUNTS
            </span>
            <span className="legend-green">
              <i /> AXIS
            </span>
          </div>
        </div>
      </div>

      <div className="container hero-rail" aria-hidden="true">
        <span>REQUIREMENT</span>
        <span>×</span>
        <span>STRUCTURED REQUEST</span>
        <span>×</span>
        <span>ENGINE</span>
        <span>×</span>
        <span>VALIDATION</span>
        <span>→</span>
        <strong>ARTIFACT + LINEAGE</strong>
      </div>
    </section>
  );
}

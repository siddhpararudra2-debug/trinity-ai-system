'use client';

import Link from 'next/link';
import type { CSSProperties } from 'react';
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

const H_LINES: { text: string; className?: string }[] = [
  { text: 'Design.' },
  { text: 'Simulate.' },
  { text: 'Validate.', className: 'accent-word' },
  { text: 'With intelligence.', className: 'h-line-soft' },
];

export default function Hero() {
  return (
    <section className="hero" id="top" aria-label="Hero">
      <div className="container hero-shell">
        <div className="hero-copy">
          <div className="hero-meta">
            <p className="eyebrow anim-rise" style={{ margin: 0, '--d': '0ms' } as CSSProperties}>
              TRINITY / ENGINEERING INTELLIGENCE — V1 OPERATING SYSTEM
            </p>
          </div>

          <h1 className="hero-title">
            {H_LINES.map((line, i) => (
              <span className="h-line" key={line.text}>
                <span
                  className={`h-line-inner ${line.className ?? ''}`}
                  style={{ '--d': `${120 + i * 110}ms` } as CSSProperties}
                >
                  {line.text}
                </span>
              </span>
            ))}
          </h1>

          <div className="hero-copy-bottom">
            <p className="lede anim-rise" style={{ '--d': '580ms' } as CSSProperties}>
              Turn natural-language engineering requirements into structured
              specifications, geometry, validation data and production-ready
              artifacts — deterministically.
            </p>
            <div>
              <div className="hero-actions anim-rise" style={{ '--d': '700ms' } as CSSProperties}>
                <Link href="#workspace" className="btn btn-primary">
                  START ENGINEERING <span aria-hidden="true">→</span>
                </Link>
                <Link href="#systems" className="text-link" style={{ marginTop: 0, padding: '12px 0' }}>
                  Explore system <span aria-hidden="true">→</span>
                </Link>
              </div>
              <div className="proof-line anim-rise" style={{ '--d': '820ms' } as CSSProperties}>
                <span className="proof-dot" aria-hidden="true" />
                <span>
                  V1 flagship: validated 50 mm quadcopter drone — checksummed
                  STL, GLB and JSON.
                </span>
              </div>
            </div>
          </div>
        </div>

        <div
          className="stage anim-stage"
          role="img"
          aria-label="Interactive 3D engineering visualization of a 50 mm quadcopter drone with spinning propellers and dimension annotations"
        >
          <HeroScene />
          <div className="stage-frame" aria-hidden="true" />
          <div className="stage-top">
            <span>TRINITY / GEOMETRY ENGINE — 50.00 MM</span>
            <span className="stage-live">
              <i aria-hidden="true" /> LIVE
            </span>
          </div>
          <div className="stage-spec anim-rise" style={{ '--d': '900ms' } as CSSProperties} aria-hidden="true">
            <div className="stage-spec-title">3D SPEC — NANO QUADCOPTER</div>
            <dl>
              <div><dt>WHEELBASE</dt><dd>50.00 MM</dd></div>
              <div><dt>PROPELLER</dt><dd>Ø 26.00 MM</dd></div>
              <div><dt>PLATE</dt><dd>21.00 × 21.00 × 3.0</dd></div>
              <div><dt>MOTOR</dt><dd>0802 · CW / CCW</dd></div>
              <div><dt>MASS EST.</dt><dd>24.6 G</dd></div>
              <div><dt>BATTERY</dt><dd>1S · 300 MAH</dd></div>
            </dl>
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
            <span className="anim-rise" style={{ '--d': '950ms' } as CSSProperties}>
              <i /> FRAME
            </span>
            <span className="legend-violet anim-rise" style={{ '--d': '1020ms' } as CSSProperties}>
              <i /> PROPS
            </span>
            <span className="legend-amber anim-rise" style={{ '--d': '1090ms' } as CSSProperties}>
              <i /> LED
            </span>
            <span className="legend-green anim-rise" style={{ '--d': '1160ms' } as CSSProperties}>
              <i /> AXIS
            </span>
          </div>
        </div>
      </div>

      <div className="container hero-rail" aria-hidden="true">
        <span style={{ '--d': '900ms' } as CSSProperties}>REQUIREMENT</span>
        <span style={{ '--d': '980ms' } as CSSProperties}>×</span>
        <span style={{ '--d': '1060ms' } as CSSProperties}>STRUCTURED REQUEST</span>
        <span style={{ '--d': '1140ms' } as CSSProperties}>×</span>
        <span style={{ '--d': '1220ms' } as CSSProperties}>ENGINE</span>
        <span style={{ '--d': '1300ms' } as CSSProperties}>×</span>
        <span style={{ '--d': '1380ms' } as CSSProperties}>VALIDATION</span>
        <span style={{ '--d': '1460ms' } as CSSProperties}>→</span>
        <strong style={{ '--d': '1560ms' } as CSSProperties}>ARTIFACT + LINEAGE</strong>
      </div>
    </section>
  );
}

'use client';

import Link from 'next/link';
import { useState } from 'react';

const CATEGORIES = ['AEROSPACE', 'ROBOTICS', 'DRONES', 'MECHANICAL', 'AUTOMATION', 'SIMULATION'] as const;

const PROJECTS = [
  {
    id: 'quad-50',
    name: '50 MM QUADCOPTER',
    category: 'AERIAL SYSTEMS',
    status: 'ACTIVE',
    dims: '50 × 50 × 8 MM',
    artifacts: 12,
    image: '/images/project-1.jpg',
    href: '#workspace',
  },
  {
    id: 'frame-75',
    name: '75 MM RACING FRAME',
    category: 'DRONES',
    status: 'VALIDATED',
    dims: '75 × 75 × 9 MM',
    artifacts: 8,
    image: '/images/project-2.jpg',
    href: '#workspace',
  },
  {
    id: 'arm-test',
    name: 'ARM STRESS STUDY',
    category: 'SIMULATION',
    status: 'DRAFT',
    dims: '50 × 12 × 3 MM',
    artifacts: 4,
    image: '/images/project-3.jpg',
    href: '#workspace',
  },
  {
    id: 'hub-v2',
    name: 'HUB V2 — TITANIUM',
    category: 'MECHANICAL',
    status: 'ACTIVE',
    dims: '32 × 32 × 6 MM',
    artifacts: 6,
    image: '/images/project-4.jpg',
    href: '#workspace',
  },
  {
    id: 'gimbal',
    name: '2-AXIS GIMBAL',
    category: 'ROBOTICS',
    status: 'SCAFFOLD',
    dims: '48 × 48 × 22 MM',
    artifacts: 2,
    image: '/images/project-5.jpg',
    href: '#workspace',
  },
  {
    id: 'enclosure',
    name: 'AVIONICS ENCLOSURE',
    category: 'AEROSPACE',
    status: 'VALIDATED',
    dims: '90 × 60 × 18 MM',
    artifacts: 9,
    image: '/images/project-6.jpg',
    href: '#workspace',
  },
];

export default function Projects() {
  const [activeCat, setActiveCat] = useState<string | null>(null);
  return (
    <section className="projects-section" id="projects" aria-labelledby="projects-title">
      <div className="container">
        <div className="section-label">07 / PROJECTS</div>
        <div className="systems-head">
          <div>
            <h2 id="projects-title">Engineering work, organized as systems.</h2>
          </div>
          <p className="lede">
            Every execution is a tracked job with artifacts and validation lineage —
            projects give that lineage a home.
          </p>
        </div>

        <div className="category-strip" role="tablist" aria-label="Engineering categories">
          {CATEGORIES.map((cat) => (
            <button
              key={cat}
              className={`category-chip ${activeCat === cat ? 'is-active' : ''}`}
              onClick={() => setActiveCat(activeCat === cat ? null : cat)}
              type="button"
              role="tab"
              aria-selected={activeCat === cat}
            >
              {cat}
            </button>
          ))}
        </div>

        <div className="projects-grid">
          {(activeCat ? PROJECTS.map((p) => ({ ...p, dimmed: !p.category.includes(activeCat) && p.category !== activeCat })) : PROJECTS.map((p) => ({ ...p, dimmed: false }))).map((p) => (
            <article key={p.id} className="project-card" style={{ opacity: (p as unknown as { dimmed: boolean }).dimmed ? 0.42 : 1 }}>
              <div className="project-preview">
                {/* eslint-disable-next-line @next/next/no-img-element */}
                <img
                  src={p.image}
                  alt=""
                  loading="lazy"
                  onError={(e) => {
                    (e.target as HTMLImageElement).style.display = 'none';
                  }}
                />
                <span className="project-preview-placeholder" aria-hidden="true">
                  ◈
                </span>
              </div>
              <div className="project-body">
                <div className="project-meta">
                  <span>{p.category}</span>
                  <span className={`status-pill ${p.status === 'ACTIVE' || p.status === 'VALIDATED' ? 'is-live' : ''}`} style={{ padding: '3px 8px', fontSize: '0.54rem' }}>
                    {p.status}
                  </span>
                </div>
                <h3>{p.name}</h3>
                <div className="project-details">
                  <span>{p.dims}</span>
                  <span>{p.artifacts} ARTIFACTS</span>
                </div>
                <Link href={p.href} className="text-link" style={{ marginTop: 8 }}>
                  OPEN PROJECT <span aria-hidden="true">→</span>
                </Link>
              </div>
            </article>
          ))}
        </div>
      </div>
    </section>
  );
}

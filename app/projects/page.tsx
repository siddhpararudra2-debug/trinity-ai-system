'use client';

import { useState } from 'react';
import Card from '@/components/ui/Card';
import CTA from '@/components/sections/CTA';
import Link from 'next/link';

const categories = ['All', 'Industrial Automation', 'Precision Engineering', 'Smart Manufacturing', 'Quality Systems', 'R&D'];

const allProjects = [
  {
    title: 'Automated Assembly Line — Tata Motors',
    description: 'Designed and deployed a fully automated assembly line reducing cycle time by 40% and improving throughput by 3x for automotive component manufacturing.',
    category: 'Industrial Automation',
    tag: 'Industrial Automation',
    imageSrc: '/images/project-1.jpg',
    metrics: ['40% faster cycles', '3x throughput', '99.2% yield'],
  },
  {
    title: 'Precision Turbine Components — BHEL',
    description: 'Manufactured 2,000+ high-precision turbine blades with ±0.002mm tolerance for power generation applications.',
    category: 'Precision Engineering',
    tag: 'Precision Engineering',
    imageSrc: '/images/project-2.jpg',
    metrics: ['2,000+ components', '±0.002mm tolerance', 'AS9100 certified'],
  },
  {
    title: 'Smart Factory IoT System — L&T',
    description: 'Implemented a real-time IoT monitoring system across 12 production lines, achieving 99.7% uptime.',
    category: 'Smart Manufacturing',
    tag: 'Smart Manufacturing',
    imageSrc: '/images/project-3.jpg',
    metrics: ['12 production lines', '99.7% uptime', '25% energy savings'],
  },
  {
    title: 'CNC Robotic Cell — Mahindra Group',
    description: 'Integrated a multi-axis robotic CNC cell for high-speed machining of automotive drivetrain components with zero manual intervention.',
    category: 'Industrial Automation',
    tag: 'Industrial Automation',
    imageSrc: '/images/project-4.jpg',
    metrics: ['6-axis robotic cell', 'Zero manual intervention', '24/7 operation'],
  },
  {
    title: 'Medical Device Components — Medtronic',
    description: 'Precision-manufactured titanium implant components meeting FDA and CE marking requirements with full traceability documentation.',
    category: 'Precision Engineering',
    tag: 'Precision Engineering',
    imageSrc: '/images/project-5.jpg',
    metrics: ['FDA compliant', 'Ti-6Al-4V titanium', 'Full traceability'],
  },
  {
    title: 'Quality Lab Modernization — Tata Steel',
    description: 'Designed and equipped a world-class quality testing laboratory with CMM, optical measurement, and NDT capabilities.',
    category: 'Quality Systems',
    tag: 'Quality Systems',
    imageSrc: '/images/project-6.jpg',
    metrics: ['State-of-art CMM', 'NDT capabilities', 'ISO 17025'],
  },
  {
    title: 'EV Battery Pack Design — Ather Energy',
    description: 'R&D partnership for next-generation electric vehicle battery pack thermal management system, from concept through validation.',
    category: 'R&D',
    tag: 'R&D',
    imageSrc: '/images/project-7.jpg',
    metrics: ['30% better cooling', 'Patent filed', '6-month timeline'],
  },
  {
    title: 'Aerospace Bracket Manufacturing — HAL',
    description: 'High-precision aerospace structural brackets from Inconel 718, machined to AS9100D standards for defense aircraft applications.',
    category: 'Precision Engineering',
    tag: 'Precision Engineering',
    imageSrc: '/images/project-8.jpg',
    metrics: ['Inconel 718', 'Defense grade', 'Zero defects'],
  },
  {
    title: 'Warehouse Automation — Flipkart',
    description: 'End-to-end automated warehouse sortation system with conveyor networks, barcode scanners, and real-time tracking dashboards.',
    category: 'Smart Manufacturing',
    tag: 'Smart Manufacturing',
    imageSrc: '/images/project-9.jpg',
    metrics: ['500K packages/day', 'Real-time tracking', '50% faster sorting'],
  },
];

export default function ProjectsPage() {
  const [activeFilter, setActiveFilter] = useState('All');

  const filteredProjects = activeFilter === 'All'
    ? allProjects
    : allProjects.filter((p) => p.category === activeFilter);

  return (
    <>
      {/* Page Header */}
      <section className="page-header">
        <div className="container">
          <div className="page-header__breadcrumb">
            <Link href="/">Home</Link>
            <span>/</span>
            <span>Projects</span>
          </div>
          <h1 className="page-header__title">Our Projects</h1>
          <p className="page-header__subtitle">
            A showcase of engineering excellence across industries and continents.
          </p>
        </div>
      </section>

      {/* Projects Grid */}
      <section className="section">
        <div className="container">
          <div className="filters filters--center">
            {categories.map((cat) => (
              <button
                key={cat}
                className={`filter-btn ${activeFilter === cat ? 'filter-btn--active' : ''}`}
                onClick={() => setActiveFilter(cat)}
              >
                {cat}
              </button>
            ))}
          </div>

          <div className="grid grid--3">
            {filteredProjects.map((project) => (
              <Card
                key={project.title}
                title={project.title}
                description={project.description}
                tag={project.tag}
                imageSrc={project.imageSrc}
              >
                <div style={{
                  display: 'flex',
                  flexWrap: 'wrap',
                  gap: 'var(--space-2)',
                  marginTop: 'var(--space-4)',
                }}>
                  {project.metrics.map((metric) => (
                    <span
                      key={metric}
                      style={{
                        padding: 'var(--space-1) var(--space-3)',
                        background: 'var(--color-slate-100)',
                        borderRadius: 'var(--radius-full)',
                        fontSize: 'var(--text-xs)',
                        color: 'var(--color-slate-600)',
                        fontWeight: 'var(--font-weight-medium)',
                      }}
                    >
                      {metric}
                    </span>
                  ))}
                </div>
              </Card>
            ))}
          </div>
        </div>
      </section>

      <CTA />
    </>
  );
}

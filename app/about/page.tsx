import type { Metadata } from 'next';
import SectionHeading from '@/components/ui/SectionHeading';
import CTA from '@/components/sections/CTA';
import Link from 'next/link';

export const metadata: Metadata = {
  title: 'About Us',
  description:
    'Learn about TRINITY Systems — 25+ years of engineering excellence in precision manufacturing, industrial automation, and R&D consulting across 12 countries.',
};

const timeline = [
  {
    year: '1998',
    title: 'Foundation',
    description: 'TRINITY Systems was founded in Noida, India, with a vision to deliver world-class precision engineering services to the manufacturing industry.',
  },
  {
    year: '2003',
    title: 'ISO 9001 Certification',
    description: 'Achieved ISO 9001 certification, establishing rigorous quality management systems and cementing our reputation for zero-defect manufacturing.',
  },
  {
    year: '2008',
    title: 'International Expansion',
    description: 'Expanded operations to serve clients in Southeast Asia and the Middle East, opening satellite offices in Singapore and Dubai.',
  },
  {
    year: '2013',
    title: 'Automation Division Launch',
    description: 'Launched our Industrial Automation division, integrating robotics and PLC programming capabilities into our service portfolio.',
  },
  {
    year: '2018',
    title: 'Smart Manufacturing Hub',
    description: 'Established our state-of-the-art Smart Manufacturing Hub featuring IoT-enabled production lines and Industry 4.0 technologies.',
  },
  {
    year: '2023',
    title: 'Global Recognition',
    description: 'Recognized as a Top 50 Engineering Services Provider in Asia-Pacific. Crossed 500+ completed projects across 12 countries.',
  },
];

const values = [
  {
    icon: '⚙️',
    title: 'Precision',
    text: 'We pursue perfection in every component, every process, and every delivery. Precision is not just what we do — it defines who we are.',
  },
  {
    icon: '💡',
    title: 'Innovation',
    text: 'We continuously invest in R&D and emerging technologies to stay ahead of industry curves and deliver cutting-edge solutions.',
  },
  {
    icon: '🤝',
    title: 'Partnership',
    text: 'We build deep, lasting relationships with our clients, working as an extension of their engineering teams to achieve shared goals.',
  },
  {
    icon: '🛡️',
    title: 'Integrity',
    text: 'Transparency, accountability, and ethical business practices form the foundation of every engagement and decision we make.',
  },
];

export default function AboutPage() {
  return (
    <>
      {/* Page Header */}
      <section className="page-header">
        <div className="container">
          <div className="page-header__breadcrumb">
            <Link href="/">Home</Link>
            <span>/</span>
            <span>About Us</span>
          </div>
          <h1 className="page-header__title">About TRINITY Systems</h1>
          <p className="page-header__subtitle">
            25+ years of engineering excellence, building the future of precision manufacturing.
          </p>
        </div>
      </section>

      {/* Mission & Vision */}
      <section className="section">
        <div className="container">
          <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 'var(--space-12)', alignItems: 'center' }}>
            <div>
              <SectionHeading
                label="Our Mission"
                title="Engineering a Better World"
              />
              <p style={{ color: 'var(--color-slate-600)', lineHeight: 1.8, marginBottom: 'var(--space-6)' }}>
                At TRINITY Systems, our mission is to empower industries with precision engineering
                and innovative manufacturing solutions that set new benchmarks for quality, efficiency,
                and sustainability.
              </p>
              <p style={{ color: 'var(--color-slate-600)', lineHeight: 1.8 }}>
                We believe that exceptional engineering is the catalyst for progress. By combining
                deep technical expertise with a relentless commitment to quality, we help our clients
                transform their manufacturing capabilities and compete on the global stage.
              </p>
            </div>
            <div style={{
              background: 'linear-gradient(135deg, var(--color-primary-50), var(--color-slate-100))',
              borderRadius: 'var(--radius-2xl)',
              padding: 'var(--space-12)',
              textAlign: 'center',
            }}>
              <div style={{ fontSize: '64px', marginBottom: 'var(--space-6)' }}>🎯</div>
              <h3 style={{ fontSize: 'var(--text-2xl)', marginBottom: 'var(--space-4)' }}>Our Vision</h3>
              <p style={{ color: 'var(--color-slate-600)', lineHeight: 1.8 }}>
                To be the most trusted engineering partner globally, recognized for our unwavering
                commitment to precision, innovation, and the advancement of manufacturing technology
                for a sustainable future.
              </p>
            </div>
          </div>
        </div>
      </section>

      {/* Core Values */}
      <section className="section section--alt">
        <div className="container">
          <SectionHeading
            label="Core Values"
            title="What Drives Us"
            description="Our values are the foundation of everything we build, every partnership we forge, and every challenge we overcome."
            centered
          />

          <div className="grid grid--4">
            {values.map((value) => (
              <div className="value-card" key={value.title}>
                <div className="value-card__icon">{value.icon}</div>
                <h3 className="value-card__title">{value.title}</h3>
                <p className="value-card__text">{value.text}</p>
              </div>
            ))}
          </div>
        </div>
      </section>

      {/* Timeline */}
      <section className="section">
        <div className="container">
          <SectionHeading
            label="Our Journey"
            title="Milestones That Define Us"
            description="A quarter-century of growth, innovation, and relentless pursuit of engineering excellence."
          />

          <div className="timeline" style={{ maxWidth: '720px' }}>
            {timeline.map((item) => (
              <div className="timeline__item" key={item.year}>
                <div className="timeline__dot" />
                <div className="timeline__year">{item.year}</div>
                <h3 className="timeline__title">{item.title}</h3>
                <p className="timeline__description">{item.description}</p>
              </div>
            ))}
          </div>
        </div>
      </section>

      {/* Certifications */}
      <section className="section section--alt">
        <div className="container">
          <SectionHeading
            label="Certifications"
            title="Industry-Recognized Standards"
            description="Our certifications reflect our commitment to the highest global standards in quality, safety, and environmental management."
            centered
          />

          <div className="grid grid--4" style={{ textAlign: 'center' }}>
            {[
              { name: 'ISO 9001:2015', desc: 'Quality Management Systems' },
              { name: 'ISO 14001:2015', desc: 'Environmental Management' },
              { name: 'AS9100D', desc: 'Aerospace Quality Standard' },
              { name: 'IATF 16949', desc: 'Automotive Quality Standard' },
            ].map((cert) => (
              <div className="card" key={cert.name} style={{ textAlign: 'center' }}>
                <div style={{
                  width: '72px',
                  height: '72px',
                  borderRadius: 'var(--radius-full)',
                  background: 'var(--color-primary-50)',
                  display: 'flex',
                  alignItems: 'center',
                  justifyContent: 'center',
                  margin: '0 auto var(--space-4)',
                  color: 'var(--color-primary)',
                  fontSize: '28px',
                }}>
                  <svg width="32" height="32" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                    <path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/>
                    <path d="m9 12 2 2 4-4"/>
                  </svg>
                </div>
                <h4 className="card__title" style={{ textAlign: 'center' }}>{cert.name}</h4>
                <p className="card__description" style={{ textAlign: 'center' }}>{cert.desc}</p>
              </div>
            ))}
          </div>
        </div>
      </section>

      <CTA />
    </>
  );
}

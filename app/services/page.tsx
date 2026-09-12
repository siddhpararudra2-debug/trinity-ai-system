import type { Metadata } from 'next';
import CTA from '@/components/sections/CTA';
import Link from 'next/link';

export const metadata: Metadata = {
  title: 'Services',
  description:
    'Explore TRINITY Systems comprehensive engineering services: Precision Engineering, Industrial Automation, Quality Assurance, R&D Consulting, Manufacturing Solutions, and Project Management.',
};

const services = [
  {
    id: 'precision',
    icon: (
      <svg width="32" height="32" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
        <circle cx="12" cy="12" r="3"/><path d="M12 1v4M12 19v4M4.22 4.22l2.83 2.83M16.95 16.95l2.83 2.83M1 12h4M19 12h4M4.22 19.78l2.83-2.83M16.95 7.05l2.83-2.83"/>
      </svg>
    ),
    title: 'Precision Engineering',
    subtitle: 'Ultra-precise manufacturing at the micro level',
    description: 'Our precision engineering services deliver components with tolerances down to ±0.001mm. From CNC machining and EDM to grinding and lapping, we handle the most demanding specifications across aerospace, defense, medical, and automotive industries.',
    capabilities: [
      '5-axis CNC machining centers',
      'Wire & sinker EDM',
      'Precision grinding & lapping',
      'Micro-machining capabilities',
      'Rapid prototyping to mass production',
      'Multi-material expertise (titanium, inconel, ceramics)',
    ],
  },
  {
    id: 'automation',
    icon: (
      <svg width="32" height="32" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
        <rect x="2" y="6" width="20" height="12" rx="2"/><path d="M12 12h.01"/><path d="M17 12h.01"/><path d="M7 12h.01"/>
      </svg>
    ),
    title: 'Industrial Automation',
    subtitle: 'Smart factory systems for maximum efficiency',
    description: 'We design and implement end-to-end industrial automation solutions that transform manufacturing floors into intelligent, connected ecosystems. Our expertise spans PLC/SCADA programming, robotic integration, and IoT-enabled smart factory systems.',
    capabilities: [
      'PLC/SCADA system design & programming',
      'Robotic arm integration (ABB, FANUC, KUKA)',
      'Conveyor & material handling systems',
      'IoT sensor networks & real-time monitoring',
      'Digital twin implementation',
      'Predictive maintenance systems',
    ],
  },
  {
    id: 'quality',
    icon: (
      <svg width="32" height="32" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
        <path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/><path d="m9 12 2 2 4-4"/>
      </svg>
    ),
    title: 'Quality Assurance',
    subtitle: 'Zero-defect manufacturing, every time',
    description: 'Our ISO 9001:2015 and AS9100D certified quality management systems ensure every component meets the highest industry standards. We employ advanced metrology, CMM inspections, and statistical process control to guarantee precision and reliability.',
    capabilities: [
      'Coordinate Measuring Machine (CMM) inspections',
      'Optical & laser measurement systems',
      'Surface roughness analysis',
      'Non-destructive testing (NDT)',
      'Statistical Process Control (SPC)',
      'First Article Inspection (FAI) reports',
    ],
  },
  {
    id: 'consulting',
    icon: (
      <svg width="32" height="32" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
        <path d="M2 3h6a4 4 0 0 1 4 4v14a3 3 0 0 0-3-3H2z"/><path d="M22 3h-6a4 4 0 0 0-4 4v14a3 3 0 0 1 3-3h7z"/>
      </svg>
    ),
    title: 'R&D Consulting',
    subtitle: 'From concept to commercialization',
    description: 'Our R&D consulting team bridges the gap between innovative ideas and market-ready products. We provide strategic guidance, feasibility studies, and hands-on development support to accelerate your innovation pipeline.',
    capabilities: [
      'Feasibility studies & concept validation',
      'Design for Manufacturing (DFM) analysis',
      'Material selection & testing',
      'Prototype development & iteration',
      'Technology scouting & evaluation',
      'Patent landscape analysis',
    ],
  },
  {
    id: 'manufacturing',
    icon: (
      <svg width="32" height="32" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
        <path d="M2 20a2 2 0 0 0 2 2h16a2 2 0 0 0 2-2V8l-7 5V8l-7 5V4a2 2 0 0 0-2-2H4a2 2 0 0 0-2 2Z"/>
      </svg>
    ),
    title: 'Manufacturing Solutions',
    subtitle: 'Scalable production for any volume',
    description: 'Whether you need a single prototype or a production run of 100,000 units, our flexible manufacturing capabilities scale seamlessly to meet your needs. We optimize for cost, quality, and lead time at every production volume.',
    capabilities: [
      'Low to high-volume production',
      'Sheet metal fabrication & welding',
      'Casting & forging services',
      'Surface treatment & finishing',
      'Assembly & sub-assembly services',
      'Just-in-time (JIT) delivery programs',
    ],
  },
  {
    id: 'management',
    icon: (
      <svg width="32" height="32" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
        <path d="M16 20V4a2 2 0 0 0-2-2h-4a2 2 0 0 0-2 2v16"/><rect x="2" y="10" width="6" height="12" rx="1"/><rect x="16" y="6" width="6" height="16" rx="1"/>
      </svg>
    ),
    title: 'Project Management',
    subtitle: 'End-to-end engineering project delivery',
    description: 'Our experienced project managers lead complex engineering initiatives from inception through commissioning. We use proven methodologies and tools to ensure on-time, on-budget delivery while managing risks proactively.',
    capabilities: [
      'Turnkey project delivery',
      'PMBOK/PRINCE2 methodologies',
      'Risk assessment & mitigation',
      'Vendor management & procurement',
      'Progress tracking & reporting',
      'Commissioning & handover support',
    ],
  },
];

export default function ServicesPage() {
  return (
    <>
      {/* Page Header */}
      <section className="page-header">
        <div className="container">
          <div className="page-header__breadcrumb">
            <Link href="/">Home</Link>
            <span>/</span>
            <span>Services</span>
          </div>
          <h1 className="page-header__title">Our Services</h1>
          <p className="page-header__subtitle">
            Comprehensive engineering and manufacturing services tailored to your industry&apos;s unique challenges.
          </p>
        </div>
      </section>

      {/* Services List */}
      {services.map((service, index) => (
        <section
          key={service.id}
          id={service.id}
          className={`section ${index % 2 === 1 ? 'section--alt' : ''}`}
        >
          <div className="container">
            <div style={{
              display: 'grid',
              gridTemplateColumns: '1fr 1fr',
              gap: 'var(--space-16)',
              alignItems: 'center',
            }}>
              <div style={{ order: index % 2 === 1 ? 2 : 1 }}>
                <div style={{
                  display: 'inline-flex',
                  alignItems: 'center',
                  justifyContent: 'center',
                  width: '64px',
                  height: '64px',
                  borderRadius: 'var(--radius-xl)',
                  background: 'var(--color-primary-50)',
                  color: 'var(--color-primary)',
                  marginBottom: 'var(--space-6)',
                }}>
                  {service.icon}
                </div>
                <h2 style={{
                  fontSize: 'var(--text-3xl)',
                  fontWeight: 'var(--font-weight-bold)',
                  color: 'var(--color-slate-900)',
                  marginBottom: 'var(--space-2)',
                }}>
                  {service.title}
                </h2>
                <p style={{
                  fontSize: 'var(--text-lg)',
                  color: 'var(--color-primary)',
                  fontWeight: 'var(--font-weight-medium)',
                  marginBottom: 'var(--space-6)',
                }}>
                  {service.subtitle}
                </p>
                <p style={{
                  color: 'var(--color-slate-600)',
                  lineHeight: 1.8,
                  marginBottom: 'var(--space-8)',
                }}>
                  {service.description}
                </p>
              </div>

              <div style={{ order: index % 2 === 1 ? 1 : 2 }}>
                <div style={{
                  background: 'var(--color-white)',
                  borderRadius: 'var(--radius-xl)',
                  padding: 'var(--space-8)',
                  border: '1px solid var(--color-slate-200)',
                }}>
                  <h4 style={{
                    fontSize: 'var(--text-sm)',
                    fontWeight: 'var(--font-weight-semibold)',
                    color: 'var(--color-slate-900)',
                    textTransform: 'uppercase',
                    letterSpacing: '0.05em',
                    marginBottom: 'var(--space-5)',
                  }}>
                    Key Capabilities
                  </h4>
                  <ul style={{ display: 'flex', flexDirection: 'column', gap: 'var(--space-4)' }}>
                    {service.capabilities.map((cap) => (
                      <li
                        key={cap}
                        style={{
                          display: 'flex',
                          alignItems: 'flex-start',
                          gap: 'var(--space-3)',
                          fontSize: 'var(--text-sm)',
                          color: 'var(--color-slate-600)',
                        }}
                      >
                        <svg
                          width="18"
                          height="18"
                          viewBox="0 0 24 24"
                          fill="none"
                          stroke="var(--color-primary)"
                          strokeWidth="2"
                          strokeLinecap="round"
                          strokeLinejoin="round"
                          style={{ minWidth: '18px', marginTop: '2px' }}
                        >
                          <path d="m9 12 2 2 4-4" />
                          <circle cx="12" cy="12" r="10" />
                        </svg>
                        {cap}
                      </li>
                    ))}
                  </ul>
                </div>
              </div>
            </div>
          </div>
        </section>
      ))}

      <CTA />
    </>
  );
}

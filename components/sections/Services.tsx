import SectionHeading from '@/components/ui/SectionHeading';
import Card from '@/components/ui/Card';

const services = [
  {
    icon: (
      <svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
        <circle cx="12" cy="12" r="3"/><path d="M12 1v4M12 19v4M4.22 4.22l2.83 2.83M16.95 16.95l2.83 2.83M1 12h4M19 12h4M4.22 19.78l2.83-2.83M16.95 7.05l2.83-2.83"/>
      </svg>
    ),
    title: 'Precision Engineering',
    description: 'Ultra-precise manufacturing with tolerances down to ±0.001mm. From prototyping to mass production, we deliver components that meet the most demanding specifications.',
    href: '/services#precision',
  },
  {
    icon: (
      <svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
        <rect x="2" y="6" width="20" height="12" rx="2"/><path d="M12 12h.01"/><path d="M17 12h.01"/><path d="M7 12h.01"/>
      </svg>
    ),
    title: 'Industrial Automation',
    description: 'End-to-end automation solutions integrating robotics, PLC programming, and IoT-enabled smart factory systems to maximize efficiency and minimize downtime.',
    href: '/services#automation',
  },
  {
    icon: (
      <svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
        <path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/>
        <path d="m9 12 2 2 4-4"/>
      </svg>
    ),
    title: 'Quality Assurance',
    description: 'ISO 9001:2015 certified quality management with advanced metrology, CMM inspections, and rigorous testing protocols ensuring zero-defect delivery.',
    href: '/services#quality',
  },
  {
    icon: (
      <svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
        <path d="M2 3h6a4 4 0 0 1 4 4v14a3 3 0 0 0-3-3H2z"/><path d="M22 3h-6a4 4 0 0 0-4 4v14a3 3 0 0 1 3-3h7z"/>
      </svg>
    ),
    title: 'R&D Consulting',
    description: 'Strategic research and development consulting that bridges the gap between concept and commercialization. We help turn innovative ideas into market-ready products.',
    href: '/services#consulting',
  },
];

export default function Services() {
  return (
    <section className="section" id="services-preview">
      <div className="container">
        <SectionHeading
          label="What We Do"
          title="Engineering Solutions That Drive Progress"
          description="From concept to delivery, we provide comprehensive engineering and manufacturing services tailored to your industry's unique challenges."
          centered
        />

        <div className="grid grid--4">
          {services.map((service) => (
            <Card
              key={service.title}
              icon={service.icon}
              title={service.title}
              description={service.description}
              href={service.href}
            />
          ))}
        </div>
      </div>
    </section>
  );
}

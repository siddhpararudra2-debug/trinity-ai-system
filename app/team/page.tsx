import type { Metadata } from 'next';
import SectionHeading from '@/components/ui/SectionHeading';
import CTA from '@/components/sections/CTA';
import Link from 'next/link';

export const metadata: Metadata = {
  title: 'Our Team',
  description:
    'Meet the leadership and engineering team behind TRINITY Systems — 150+ engineers driving precision, innovation, and manufacturing excellence.',
};

const leadership = [
  {
    name: 'Vikram Mehta',
    role: 'Founder & CEO',
    bio: '30+ years in precision engineering. Former Head of Manufacturing at Tata Advanced Systems. IIT Delhi alumnus with a vision for world-class Indian engineering.',
    initials: 'VM',
  },
  {
    name: 'Dr. Ananya Rao',
    role: 'Chief Technology Officer',
    bio: 'PhD in Mechanical Engineering from IISc Bangalore. Led R&D teams at ISRO and DRDO before joining TRINITY to drive innovation in smart manufacturing.',
    initials: 'AR',
  },
  {
    name: 'Sanjay Kumar',
    role: 'VP of Operations',
    bio: 'PRINCE2 certified project management professional with 20+ years optimizing manufacturing operations across automotive and aerospace sectors.',
    initials: 'SK',
  },
  {
    name: 'Priya Deshmukh',
    role: 'Head of Quality',
    bio: 'Six Sigma Master Black Belt with expertise in metrology, CMM programming, and quality management systems. 15+ years ensuring zero-defect delivery.',
    initials: 'PD',
  },
  {
    name: 'Arjun Reddy',
    role: 'Director of Automation',
    bio: 'Specialist in PLC programming, robotic integration, and Industry 4.0 technologies. Designed automation solutions for 50+ manufacturing plants globally.',
    initials: 'AR',
  },
  {
    name: 'Meera Joshi',
    role: 'Head of R&D',
    bio: 'Materials science expert with 12 patents in advanced manufacturing processes. Leads TRINITY\'s innovation lab focused on next-generation engineering solutions.',
    initials: 'MJ',
  },
];

const departments = [
  {
    name: 'Engineering',
    count: '80+',
    description: 'Mechanical, electrical, and software engineers delivering precision solutions.',
  },
  {
    name: 'Quality & Metrology',
    count: '25+',
    description: 'Inspection specialists and quality engineers ensuring every component meets spec.',
  },
  {
    name: 'R&D & Innovation',
    count: '20+',
    description: 'Researchers and designers pushing the boundaries of manufacturing technology.',
  },
  {
    name: 'Operations & PMO',
    count: '30+',
    description: 'Project managers and operations staff orchestrating seamless delivery.',
  },
];

export default function TeamPage() {
  return (
    <>
      {/* Page Header */}
      <section className="page-header">
        <div className="container">
          <div className="page-header__breadcrumb">
            <Link href="/">Home</Link>
            <span>/</span>
            <span>Team</span>
          </div>
          <h1 className="page-header__title">Our Team</h1>
          <p className="page-header__subtitle">
            150+ engineers, researchers, and project leaders united by a passion for engineering excellence.
          </p>
        </div>
      </section>

      {/* Leadership */}
      <section className="section">
        <div className="container">
          <SectionHeading
            label="Leadership"
            title="Meet Our Executive Team"
            description="The visionaries and domain experts guiding TRINITY Systems' mission to redefine engineering excellence."
            centered
          />

          <div className="grid grid--3">
            {leadership.map((person) => (
              <div className="team-card" key={person.name}>
                <div
                  className="team-card__avatar"
                  style={{
                    background: 'linear-gradient(135deg, var(--color-primary-100), var(--color-primary-200))',
                    display: 'flex',
                    alignItems: 'center',
                    justifyContent: 'center',
                    fontSize: 'var(--text-2xl)',
                    fontWeight: 'var(--font-weight-bold)',
                    color: 'var(--color-primary-dark)',
                  }}
                >
                  {person.initials}
                </div>
                <h3 className="team-card__name">{person.name}</h3>
                <p className="team-card__role">{person.role}</p>
                <p className="team-card__bio">{person.bio}</p>
                <div className="team-card__social">
                  <a href="#" className="team-card__social-link" aria-label={`${person.name} LinkedIn`}>
                    <svg width="14" height="14" viewBox="0 0 24 24" fill="currentColor">
                      <path d="M20.447 20.452h-3.554v-5.569c0-1.328-.027-3.037-1.852-3.037-1.853 0-2.136 1.445-2.136 2.939v5.667H9.351V9h3.414v1.561h.046c.477-.9 1.637-1.85 3.37-1.85 3.601 0 4.267 2.37 4.267 5.455v6.286zM5.337 7.433c-1.144 0-2.063-.926-2.063-2.065 0-1.138.92-2.063 2.063-2.063 1.14 0 2.064.925 2.064 2.063 0 1.139-.925 2.065-2.064 2.065zm1.782 13.019H3.555V9h3.564v11.452zM22.225 0H1.771C.792 0 0 .774 0 1.729v20.542C0 23.227.792 24 1.771 24h20.451C23.2 24 24 23.227 24 22.271V1.729C24 .774 23.2 0 22.222 0h.003z"/>
                    </svg>
                  </a>
                  <a href="#" className="team-card__social-link" aria-label={`${person.name} Email`}>
                    <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                      <rect x="2" y="4" width="20" height="16" rx="2"/><path d="m22 7-8.97 5.7a1.94 1.94 0 0 1-2.06 0L2 7"/>
                    </svg>
                  </a>
                </div>
              </div>
            ))}
          </div>
        </div>
      </section>

      {/* Departments */}
      <section className="section section--alt">
        <div className="container">
          <SectionHeading
            label="Our Departments"
            title="Specialized Teams, Unified Vision"
            description="Each department brings deep domain expertise to deliver comprehensive engineering solutions."
            centered
          />

          <div className="grid grid--4">
            {departments.map((dept) => (
              <div className="card" key={dept.name} style={{ textAlign: 'center' }}>
                <div style={{
                  fontSize: 'var(--text-4xl)',
                  fontWeight: 'var(--font-weight-bold)',
                  color: 'var(--color-primary)',
                  marginBottom: 'var(--space-2)',
                }}>
                  {dept.count}
                </div>
                <h3 className="card__title" style={{ textAlign: 'center' }}>{dept.name}</h3>
                <p className="card__description" style={{ textAlign: 'center' }}>{dept.description}</p>
              </div>
            ))}
          </div>
        </div>
      </section>

      {/* Join Us */}
      <section className="section">
        <div className="container" style={{ textAlign: 'center', maxWidth: '640px' }}>
          <SectionHeading
            label="Careers"
            title="Join the TRINITY Team"
            description="We're always looking for talented engineers, researchers, and project leaders who share our passion for precision and innovation."
            centered
          />
          <Link
            href="/contact"
            className="btn btn--primary btn--lg"
            style={{ margin: '0 auto' }}
          >
            View Open Positions
            <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
              <path d="M5 12h14" /><path d="m12 5 7 7-7 7" />
            </svg>
          </Link>
        </div>
      </section>

      <CTA />
    </>
  );
}

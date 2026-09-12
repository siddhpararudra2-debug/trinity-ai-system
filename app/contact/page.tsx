import type { Metadata } from 'next';
import SectionHeading from '@/components/ui/SectionHeading';
import ContactForm from '@/components/sections/ContactForm';
import Link from 'next/link';

export const metadata: Metadata = {
  title: 'Contact Us',
  description:
    'Get in touch with TRINITY Systems for precision engineering, industrial automation, and manufacturing solutions. Request a quote or schedule a consultation.',
};

const contactInfo = [
  {
    icon: (
      <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
        <path d="M20 10c0 6-8 12-8 12s-8-6-8-12a8 8 0 0 1 16 0Z"/>
        <circle cx="12" cy="10" r="3"/>
      </svg>
    ),
    title: 'Visit Us',
    text: '1247 Industrial Avenue, Sector 62\nNoida, Uttar Pradesh 201301, India',
  },
  {
    icon: (
      <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
        <path d="M22 16.92v3a2 2 0 0 1-2.18 2 19.79 19.79 0 0 1-8.63-3.07 19.5 19.5 0 0 1-6-6 19.79 19.79 0 0 1-3.07-8.67A2 2 0 0 1 4.11 2h3a2 2 0 0 1 2 1.72c.127.96.361 1.903.7 2.81a2 2 0 0 1-.45 2.11L8.09 9.91a16 16 0 0 0 6 6l1.27-1.27a2 2 0 0 1 2.11-.45c.907.339 1.85.573 2.81.7A2 2 0 0 1 22 16.92z"/>
      </svg>
    ),
    title: 'Call Us',
    text: '+91 120 456 7890\n+91 120 456 7891',
  },
  {
    icon: (
      <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
        <rect x="2" y="4" width="20" height="16" rx="2"/>
        <path d="m22 7-8.97 5.7a1.94 1.94 0 0 1-2.06 0L2 7"/>
      </svg>
    ),
    title: 'Email Us',
    text: 'info@trinitysystems.com\nsales@trinitysystems.com',
  },
  {
    icon: (
      <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
        <circle cx="12" cy="12" r="10"/><polyline points="12 6 12 12 16 14"/>
      </svg>
    ),
    title: 'Business Hours',
    text: 'Mon — Fri: 9:00 AM — 6:00 PM\nSat: 9:00 AM — 1:00 PM',
  },
];

export default function ContactPage() {
  return (
    <>
      {/* Page Header */}
      <section className="page-header">
        <div className="container">
          <div className="page-header__breadcrumb">
            <Link href="/">Home</Link>
            <span>/</span>
            <span>Contact</span>
          </div>
          <h1 className="page-header__title">Get in Touch</h1>
          <p className="page-header__subtitle">
            Ready to start your next engineering project? We&apos;d love to hear from you.
          </p>
        </div>
      </section>

      {/* Contact Info Cards */}
      <section className="section">
        <div className="container">
          <div className="grid grid--4" style={{ marginBottom: 'var(--space-16)' }}>
            {contactInfo.map((info) => (
              <div className="contact-info-card" key={info.title} style={{ flexDirection: 'column', textAlign: 'center', alignItems: 'center' }}>
                <div className="contact-info-card__icon" style={{ margin: '0 auto var(--space-4)' }}>
                  {info.icon}
                </div>
                <h3 className="contact-info-card__title">{info.title}</h3>
                <p className="contact-info-card__text" style={{ whiteSpace: 'pre-line' }}>
                  {info.text}
                </p>
              </div>
            ))}
          </div>

          {/* Contact Form & Map */}
          <div style={{
            display: 'grid',
            gridTemplateColumns: '1fr 1fr',
            gap: 'var(--space-12)',
          }}>
            <div>
              <SectionHeading
                label="Send a Message"
                title="Let's Discuss Your Project"
                description="Fill out the form below and our engineering team will respond within 24 hours."
              />
              <ContactForm />
            </div>

            <div>
              <SectionHeading
                label="Our Location"
                title="Find Us on the Map"
                description="Located in the heart of Noida's industrial corridor, easily accessible from Delhi NCR."
              />
              <div className="map-embed">
                <iframe
                  src="https://www.google.com/maps/embed?pb=!1m18!1m12!1m3!1d14012.599830744178!2d77.3601!3d28.6282!2m3!1f0!2f0!3f0!3m2!1i1024!2i768!4f13.1!3m3!1m2!1s0x390ce5a43173357b%3A0x37ffce30c87cc03f!2sSector%2062%2C%20Noida!5e0!3m2!1sen!2sin!4v1"
                  width="100%"
                  height="100%"
                  style={{ border: 0 }}
                  allowFullScreen
                  loading="lazy"
                  referrerPolicy="no-referrer-when-downgrade"
                  title="TRINITY Systems Location - Noida Sector 62"
                />
              </div>
            </div>
          </div>
        </div>
      </section>
    </>
  );
}

import Button from '@/components/ui/Button';

export default function Hero() {
  return (
    <section className="hero" id="hero">
      <div className="hero__bg">
        <div
          className="hero__bg-image"
          style={{
            background: 'linear-gradient(135deg, #1a2332 0%, #0d1b2a 30%, #0a192f 60%, #112240 100%)',
            width: '100%',
            height: '100%',
          }}
        />
      </div>
      <div className="hero__overlay" />

      <div className="container hero__content">
        <div className="hero__badge">
          <span className="hero__badge-dot" />
          Engineering Excellence Since 1998
        </div>

        <h1 className="hero__title">
          Precision Engineering<br />
          for a <span className="hero__title-accent">Smarter Future</span>
        </h1>

        <p className="hero__subtitle">
          TRINITY Systems delivers world-class manufacturing solutions, industrial automation,
          and precision engineering services that drive innovation across industries worldwide.
        </p>

        <div className="hero__actions">
          <Button variant="primary" size="lg" href="/services">
            Our Services
            <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
              <path d="M5 12h14" /><path d="m12 5 7 7-7 7" />
            </svg>
          </Button>
          <Button variant="secondary" size="lg" href="/contact">
            Contact Us
          </Button>
        </div>
      </div>

      <div className="hero__scroll-indicator">
        <div className="hero__scroll-mouse" />
        <span>Scroll to explore</span>
      </div>
    </section>
  );
}

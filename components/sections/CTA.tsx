import Button from '@/components/ui/Button';

export default function CTA() {
  return (
    <section className="cta-section" id="cta">
      <div className="container cta-section__inner">
        <h2 className="cta-section__title">
          Ready to Engineer Your Next Breakthrough?
        </h2>
        <p className="cta-section__text">
          Let&apos;s discuss how TRINITY Systems can help you achieve precision, efficiency,
          and innovation in your manufacturing processes.
        </p>
        <div className="cta-section__actions">
          <Button variant="white" size="lg" href="/contact">
            Start a Conversation
          </Button>
          <Button variant="ghost-white" size="lg" href="/projects">
            View Our Work
          </Button>
        </div>
      </div>
    </section>
  );
}

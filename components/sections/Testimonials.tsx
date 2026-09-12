'use client';

import { useState, useCallback } from 'react';
import SectionHeading from '@/components/ui/SectionHeading';

const testimonials = [
  {
    quote: 'TRINITY Systems transformed our production line with their automation expertise. The results exceeded our expectations — 40% faster cycle times and virtually zero defects.',
    name: 'Rajesh Sharma',
    role: 'VP of Manufacturing',
    company: 'Tata Motors Ltd.',
  },
  {
    quote: 'Their precision engineering capabilities are world-class. The turbine components they delivered met every specification flawlessly, and their attention to quality is unmatched.',
    name: 'Dr. Priya Nair',
    role: 'Chief Technology Officer',
    company: 'BHEL Power Systems',
  },
  {
    quote: 'Working with TRINITY on our smart factory initiative was a game-changer. Their IoT integration reduced our energy costs by 25% while improving overall equipment effectiveness.',
    name: 'Amit Patel',
    role: 'Plant Director',
    company: 'Larsen & Toubro',
  },
  {
    quote: 'From concept to delivery, TRINITY Systems demonstrated exceptional engineering competence. Their R&D consulting helped us bring our product to market 6 months ahead of schedule.',
    name: 'Sarah Chen',
    role: 'Director of Engineering',
    company: 'Siemens India',
  },
];

export default function Testimonials() {
  const [activeIndex, setActiveIndex] = useState(0);

  const goToSlide = useCallback((index: number) => {
    setActiveIndex(index);
  }, []);

  return (
    <section className="section" id="testimonials">
      <div className="container">
        <SectionHeading
          label="Client Testimonials"
          title="Trusted by Industry Leaders"
          description="Hear from the organizations we've partnered with to deliver excellence."
          centered
        />

        <div className="testimonials__carousel">
          <div
            className="testimonials__track"
            style={{ transform: `translateX(-${activeIndex * 100}%)` }}
          >
            {testimonials.map((testimonial) => (
              <div className="testimonials__slide" key={testimonial.name}>
                <div className="testimonial-card">
                  <p className="testimonial-card__quote">
                    {testimonial.quote}
                  </p>
                  <div className="testimonial-card__author">
                    <div
                      className="testimonial-card__avatar"
                      style={{
                        background: 'var(--color-primary-100)',
                        display: 'flex',
                        alignItems: 'center',
                        justifyContent: 'center',
                        fontSize: 'var(--text-xl)',
                        fontWeight: 'var(--font-weight-bold)',
                        color: 'var(--color-primary)',
                      }}
                    >
                      {testimonial.name.charAt(0)}
                    </div>
                    <div style={{ textAlign: 'left' }}>
                      <div className="testimonial-card__name">{testimonial.name}</div>
                      <div className="testimonial-card__role">
                        {testimonial.role}, {testimonial.company}
                      </div>
                    </div>
                  </div>
                </div>
              </div>
            ))}
          </div>

          <div className="testimonials__dots">
            {testimonials.map((_, index) => (
              <button
                key={index}
                className={`testimonials__dot ${index === activeIndex ? 'testimonials__dot--active' : ''}`}
                onClick={() => goToSlide(index)}
                aria-label={`Go to testimonial ${index + 1}`}
              />
            ))}
          </div>
        </div>
      </div>
    </section>
  );
}

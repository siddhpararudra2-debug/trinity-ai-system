'use client';

import { useEffect, useRef, type CSSProperties, type ReactNode } from 'react';

export type RevealVariant = 'up' | 'down' | 'left' | 'right' | 'scale' | 'fade';

type RevealProps = {
  children: ReactNode;
  delay?: number;
  className?: string;
  /** motion style — defaults to 'up' (previous behaviour) */
  variant?: RevealVariant;
  /** travel distance in px for directional variants */
  distance?: number;
};

const HIDDEN: Record<RevealVariant, string> = {
  up: 'reveal--up',
  down: 'reveal--down',
  left: 'reveal--left',
  right: 'reveal--right',
  scale: 'reveal--scale',
  fade: 'reveal--fade',
};

export default function Reveal({
  children,
  delay = 0,
  className = '',
  variant = 'up',
  distance,
}: RevealProps) {
  const ref = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const el = ref.current;
    if (!el) return;
    if (
      window.matchMedia('(prefers-reduced-motion: reduce)').matches ||
      !('IntersectionObserver' in window)
    ) {
      el.classList.add('visible');
      return;
    }
    const observer = new IntersectionObserver(
      (entries) => {
        entries.forEach((entry) => {
          if (entry.isIntersecting) {
            el.classList.add('visible');
            observer.disconnect();
          }
        });
      },
      { threshold: 0.12, rootMargin: '0px 0px -6% 0px' },
    );
    observer.observe(el);
    return () => observer.disconnect();
  }, []);

  const style = {
    '--reveal-delay': `${delay}ms`,
    ...(distance !== undefined ? { '--reveal-distance': `${distance}px` } : {}),
  } as CSSProperties;

  return (
    <div ref={ref} className={`reveal ${HIDDEN[variant]} ${className}`} style={style}>
      {children}
    </div>
  );
}

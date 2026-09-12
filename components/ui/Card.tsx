import Link from 'next/link';
import { ReactNode } from 'react';

interface CardProps {
  icon?: ReactNode;
  title: string;
  description: string;
  href?: string;
  linkText?: string;
  tag?: string;
  imageSrc?: string;
  imageAlt?: string;
  className?: string;
  children?: ReactNode;
}

export default function Card({
  icon,
  title,
  description,
  href,
  linkText = 'Learn More',
  tag,
  imageSrc,
  imageAlt,
  className = '',
  children,
}: CardProps) {
  if (imageSrc) {
    return (
      <div className={`card card--image ${className}`}>
        <div className="card__image-wrapper">
          {/* eslint-disable-next-line @next/next/no-img-element */}
          <img src={imageSrc} alt={imageAlt || title} className="card__image" />
          <div className="card__image-overlay" />
        </div>
        <div className="card__body">
          {tag && <span className="card__tag">{tag}</span>}
          <h3 className="card__title" style={{ fontSize: 'var(--text-lg)' }}>{title}</h3>
          <p className="card__description">{description}</p>
          {href && (
            <Link href={href} className="card__link">
              {linkText}
              <span className="card__link-arrow">→</span>
            </Link>
          )}
          {children}
        </div>
      </div>
    );
  }

  return (
    <div className={`card ${className}`}>
      {icon && <div className="card__icon">{icon}</div>}
      <h3 className="card__title">{title}</h3>
      <p className="card__description">{description}</p>
      {href && (
        <Link href={href} className="card__link">
          {linkText}
          <span className="card__link-arrow">→</span>
        </Link>
      )}
      {children}
    </div>
  );
}

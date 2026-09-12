interface SectionHeadingProps {
  label?: string;
  title: string;
  description?: string;
  centered?: boolean;
  className?: string;
}

export default function SectionHeading({
  label,
  title,
  description,
  centered = false,
  className = '',
}: SectionHeadingProps) {
  return (
    <div className={`section-heading ${centered ? 'section-heading--center' : ''} ${className}`}>
      {label && <p className="section-heading__label">{label}</p>}
      <h2 className="section-heading__title">{title}</h2>
      {description && <p className="section-heading__description">{description}</p>}
    </div>
  );
}

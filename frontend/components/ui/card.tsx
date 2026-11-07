import React from 'react';

type CardContentProps = {
  children: React.ReactNode;
  className?: string;
};

type CardHeaderProps = {
  children: React.ReactNode;
  className?: string;
};

export function Card({ className, children }: { className?: string; children: React.ReactNode }) {
  return (
    <div className={className || 'rounded-lg border'}>
      {children}
    </div>
  );
}

Card.displayName = 'Card';

export function CardHeader({
  className,
  children,
}: CardHeaderProps) {
  return <div className={className}>{children}</div>;
}

CardHeader.displayName = 'CardHeader';

export function CardContent({
  className,
  children,
}: CardContentProps) {
  return <div className={className}>{children}</div>;
}

CardContent.displayName = 'CardContent';
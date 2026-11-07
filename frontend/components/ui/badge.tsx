import React from 'react';

export type BadgeProps = {
  children: React.ReactNode;
  variant?: 'default' | 'destructive' | 'secondary' | 'outline';
  className?: string;
};

export function Badge({
  variant = 'default',
  className,
  children,
}: BadgeProps) {
  return (
    <span data-variant={variant} className={className}>
      {children}
    </span>
  );
}

Badge.displayName = 'Badge';

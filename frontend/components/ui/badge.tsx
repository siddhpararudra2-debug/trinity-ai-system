import React from 'react';

type BadgeProps = {
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
    <span className={className}>
      {children}
    </span>
  );
}

Badge.displayName = 'Badge';
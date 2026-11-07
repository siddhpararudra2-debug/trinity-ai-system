import React from 'react';

type ButtonProps = {
  children: React.ReactNode;
  variant?: 'default' | 'destructive' | 'secondary' | 'outline';
  size?: 'sm' | 'lg' | 'icon';
  onClick?: () => void;
  className?: string;
};

export function Button({
  variant = 'default',
  size = 'sm',
  onClick,
  className,
  children,
}: ButtonProps) {
  return (
    <button
      className={className}
      onClick={onClick}
    >
      {children}
    </button>
  );
}

Button.displayName = 'Button';
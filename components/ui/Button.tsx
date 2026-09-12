import Link from 'next/link';
import { ReactNode } from 'react';

interface ButtonProps {
  children: ReactNode;
  variant?: 'primary' | 'secondary' | 'outline' | 'dark' | 'white' | 'ghost-white';
  size?: 'sm' | 'md' | 'lg';
  href?: string;
  onClick?: () => void;
  type?: 'button' | 'submit' | 'reset';
  className?: string;
  id?: string;
  disabled?: boolean;
}

export default function Button({
  children,
  variant = 'primary',
  size = 'md',
  href,
  onClick,
  type = 'button',
  className = '',
  id,
  disabled = false,
}: ButtonProps) {
  const classes = `btn btn--${variant} ${size !== 'md' ? `btn--${size}` : ''} ${className}`.trim();

  if (href) {
    return (
      <Link href={href} className={classes} id={id}>
        {children}
      </Link>
    );
  }

  return (
    <button className={classes} onClick={onClick} type={type} id={id} disabled={disabled}>
      {children}
    </button>
  );
}

import React from 'react';

type InputProps = {
  placeholder?: string;
  value: string | number;
  onChange?: (value: string) => void;
  className?: string;
  disabled?: boolean;
};

export function Input({ placeholder, value, onChange, className, disabled }: InputProps) {
  return (
    <input
      placeholder={placeholder}
      value={typeof value === 'number' ? String(value) : value}
      onChange={(e) => onChange?.(e.target.value)}
      disabled={disabled}
      className={className}
    />
  );
}

Input.displayName = 'Input';
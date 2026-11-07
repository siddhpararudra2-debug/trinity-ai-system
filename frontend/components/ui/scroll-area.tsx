import React from 'react';

type ScrollAreaProps = {
  children: React.ReactNode;
  className?: string;
};

export function ScrollArea({ className, children }: ScrollAreaProps) {
  return (
    <div className={className ? `rounded overflow-auto ${className}` : 'rounded overflow-auto'}>
      {children}
    </div>
  );
}

ScrollArea.displayName = 'ScrollArea';
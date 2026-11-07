import React from 'react';

export type TabListProps = {
  children: React.ReactNode;
  className?: string;
  defaultValue?: string;
  value?: string;
  onValueChange?: (value: string) => void;
};

export type TabProps = {
  value: string;
  children: React.ReactNode;
  className?: string;
  onClick?: () => void;
};

export type TabPanelProps = {
  value: string;
  children: React.ReactNode;
  className?: string;
};

export function TabList({
  className,
  children,
  defaultValue,
  value,
  onValueChange,
}: TabListProps) {
  const activeValue = value ?? defaultValue;
  return (
    <div className={className} role="tablist" data-active={activeValue}>
      {React.Children.toArray(children).map((child) => {
        if (!React.isValidElement(child)) {
          return child;
        }
        const childProps = child.props as Record<string, unknown>;
        const childValue = (childProps.value as string | undefined) ?? (typeof child.key === 'string' ? child.key : undefined);
        return React.cloneElement(child as React.ReactElement<Record<string, unknown>>, {
          value: childValue,
          onValueChange,
        });
      })}
    </div>
  );
}

TabList.displayName = 'TabList';

export function Tab({ value, className, children, onClick }: TabProps) {
  return (
    <div
      role="tab"
      aria-selected={true}
      data-value={value}
      className={className}
      onClick={onClick}
    >
      {children}
    </div>
  );
}

Tab.displayName = 'Tab';

export function TabPanel({ value, className, children }: TabPanelProps) {
  return (
    <div role="tabpanel" data-value={value} className={className}>
      {children}
    </div>
  );
}

TabPanel.displayName = 'TabPanel';


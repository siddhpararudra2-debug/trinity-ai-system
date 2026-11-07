import React from 'react';

type TabListProps = {
  children: React.ReactNode;
  className?: string;
  defaultValue?: string;
  value?: string;
  onValueChange?: (value: string) => void;
};

type TabProps = {
  value: string;
  children: React.ReactNode;
  className?: string;
};

type TabPanelProps = {
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
  return (
    <div className={className} role="tablist">
      {React.Children.toArray(children).map((child) => {
        const childValue = child.props.value || child.key;
        return React.cloneElement(child, {
          value: childValue,
          onValueChange,
        });
      })}
    </div>
  );
}

TabList.displayName = 'TabList';

export function Tab({ value, className, children }: TabProps) {
  return (
    <div
      role="tab"
      selected={true}
      className={className}
      onClick={() => {}}
    >
      {children}
    </div>
  );
}

Tab.displayName = 'Tab';

export function TabPanel({ value, className, children }: TabPanelProps) {
  return <div role="tabpanel" className={className}>{children}</div>;
}

TabPanel.displayName = 'TabPanel';
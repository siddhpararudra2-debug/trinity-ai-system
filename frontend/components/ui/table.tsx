import React from 'react';

type TableRowProps = {
  children: React.ReactNode;
  className?: string;
};

type TableCellProps = {
  children: React.ReactNode;
  className?: string;
};

type TableHeaderProps = {
  children: React.ReactNode;
  className?: string;
};

type TableBodyProps = {
  children: React.ReactNode;
  className?: string;
};

export const Table = React.forwardRef<
  HTMLTableElement,
  { className?: string }
>(({ className, children }, ref) => (
  <table ref={ref} className={className}>{children}</table>
));

Table.displayName = 'Table';

export const TableHeader = ({
  className,
  children,
}: TableHeaderProps) => (
  <thead className={className}>{children}</thead>
);

TableHeader.displayName = 'TableHeader';

export const TableRow = ({
  className,
  children,
}: {
  className?: string;
  children: React.ReactNode;
}) => (
  <tr className={className}>{children}</tr>
);

TableRow.displayName = 'TableRow';

export const TableCell = ({
  className,
  children,
}: TableCellProps) => (
  <td className={className}>{children}</td>
);

TableCell.displayName = 'TableCell';

export const TableBody = ({
  className,
  children,
}: TableBodyProps) => (
  <tbody className={className}>{children}</tbody>
);

TableBody.displayName = 'TableBody';
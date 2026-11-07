import React from 'react';

export type TableRowProps = {
  children: React.ReactNode;
  className?: string;
};

export type TableCellProps = {
  children: React.ReactNode;
  className?: string;
};

export type TableHeaderProps = {
  children: React.ReactNode;
  className?: string;
};

export type TableBodyProps = {
  children: React.ReactNode;
  className?: string;
};

export const Table = React.forwardRef<
  HTMLTableElement,
  { className?: string; children?: React.ReactNode }
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
}: TableRowProps) => (
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

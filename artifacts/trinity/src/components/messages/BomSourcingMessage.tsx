import React from 'react';

export function BomSourcingMessage({ data }: { data: any }) {
  const lines = data?.lines || [];
  return (
    <div className="flex flex-col gap-3 font-mono text-sm">
      <div className="text-primary font-bold uppercase tracking-widest">BOM & Sourcing</div>
      <p className="text-xs text-muted-foreground">
        Matched {data?.summary?.matched ?? 0} / {data?.summary?.total_lines ?? lines.length} lines
      </p>
      {lines.map((line: any, index: number) => (
        <div key={`${line.reference}-${index}`} className="rounded border border-white/10 bg-black/30 p-3 text-xs">
          <div className="flex justify-between gap-2">
            <span>{line.reference || '—'} · {line.value}</span>
            <span className={line.status === 'matched' ? 'text-emerald-400' : 'text-yellow-400'}>{line.status}</span>
          </div>
          {line.sourcing?.quotes?.[0] && (
            <div className="mt-1 text-muted-foreground">
              {line.sourcing.quotes[0].distributor}: {line.sourcing.quotes[0].sku} · ${line.sourcing.quotes[0].unit_price_usd} · stock {line.sourcing.quotes[0].stock}
            </div>
          )}
        </div>
      ))}
      {data?.disclaimer && <p className="text-[10px] text-muted-foreground">{data.disclaimer}</p>}
    </div>
  );
}

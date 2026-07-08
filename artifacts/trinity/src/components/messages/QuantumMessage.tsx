import React, { useMemo } from 'react';
import { BarChart, Bar, XAxis, YAxis, Tooltip, ResponsiveContainer, CartesianGrid } from 'recharts';

export function QuantumMessage({ data }: { data: any }) {
  const chartData = useMemo(() => {
    if (!data?.counts) return [];
    return Object.entries(data.counts).map(([key, value]) => ({ state: key, count: value }));
  }, [data?.counts]);

  return (
    <div className="flex flex-col gap-4 font-mono text-sm w-full max-w-full">
      {data?.description && <p className="text-foreground/80">{data.description}</p>}
      
      {data?.circuit_diagram && (
        <div className="space-y-2">
          <div className="text-xs text-primary uppercase">CIRCUIT TOPOLOGY:</div>
          <pre className="p-4 bg-black/30 border border-white/5 rounded overflow-x-auto text-xs text-primary leading-tight">
            {data.circuit_diagram}
          </pre>
        </div>
      )}

      {chartData.length > 0 && (
        <div className="space-y-2 w-full mt-2">
          <div className="text-xs text-primary uppercase">MEASUREMENT COUNTS:</div>
          <div className="h-64 w-full bg-black/30 border border-white/5 p-4 rounded">
            <ResponsiveContainer width="100%" height="100%">
              <BarChart data={chartData} margin={{ top: 10, right: 10, left: -20, bottom: 0 }}>
                <CartesianGrid strokeDasharray="3 3" stroke="rgba(255,255,255,0.05)" vertical={false} />
                <XAxis dataKey="state" stroke="hsl(var(--muted-foreground))" fontSize={11} tickLine={false} axisLine={false} />
                <YAxis stroke="hsl(var(--muted-foreground))" fontSize={11} tickLine={false} axisLine={false} />
                <Tooltip 
                  cursor={{ fill: 'rgba(255,255,255,0.05)' }} 
                  contentStyle={{ backgroundColor: 'hsl(var(--popover))', border: '1px solid hsl(var(--border))', borderRadius: '4px', fontFamily: 'var(--font-mono)' }} 
                />
                <Bar dataKey="count" fill="hsl(var(--accent))" radius={[2, 2, 0, 0]} />
              </BarChart>
            </ResponsiveContainer>
          </div>
        </div>
      )}
    </div>
  );
}
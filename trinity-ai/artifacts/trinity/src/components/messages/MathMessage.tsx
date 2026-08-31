import React, { useMemo } from 'react';
import katex from 'katex';

export function MathMessage({ data }: { data: any }) {
  const latexHtml = useMemo(() => {
    if (!data?.latex) return '';
    try {
      return katex.renderToString(data.latex, { displayMode: true, throwOnError: false });
    } catch (e) {
      return String(data.latex)
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#039;');
    }
  }, [data?.latex]);

  return (
    <div className="flex flex-col gap-3 font-mono text-sm">
      {data?.expression && (
        <div className="text-muted-foreground break-all">
          <span className="text-primary mr-2">&gt;</span>{data.expression}
        </div>
      )}
      {latexHtml && (
        <div 
          dangerouslySetInnerHTML={{ __html: latexHtml }} 
          className="bg-black/30 p-4 rounded border border-white/5 overflow-x-auto my-2 text-foreground" 
        />
      )}
      {data?.steps && data.steps.length > 0 && (
        <div className="space-y-1">
          <div className="text-xs text-muted-foreground uppercase mb-2">EXECUTION STEPS:</div>
          {data.steps.map((step: string, i: number) => (
            <div key={i} className="flex gap-2">
              <span className="text-primary opacity-50">{(i + 1).toString().padStart(2, '0')}</span>
              <span className="text-foreground/80">{step}</span>
            </div>
          ))}
        </div>
      )}
      {data?.result && (
        <div className="font-bold text-accent text-lg mt-2 flex items-center gap-2">
          <span className="text-accent/50 text-sm font-normal">RESULT:</span>
          {data.result}
        </div>
      )}
    </div>
  );
}
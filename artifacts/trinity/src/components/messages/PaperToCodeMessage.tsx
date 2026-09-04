import React from 'react';

export function PaperToCodeMessage({ data }: { data: any }) {
  const blocks = data?.blocks || [];
  return (
    <div className="flex flex-col gap-3 font-mono text-sm">
      <div className="text-primary font-bold uppercase tracking-widest">Paper-to-Code</div>
      <p className="text-xs text-muted-foreground">Equations found: {data?.equations_found ?? blocks.length}</p>
      {blocks.map((block: any) => (
        <div key={block.index} className="rounded border border-white/10 bg-black/30 p-3">
          <div className="text-[10px] uppercase tracking-widest text-accent">Block {block.index}</div>
          <div className="mt-1 text-xs text-foreground/90">{block.source_expression}</div>
          {block.latex && <div className="mt-2 text-[10px] text-muted-foreground">LaTeX: {block.latex}</div>}
          {block.result && <div className="mt-1 text-[10px] text-primary">SymPy: {block.result}</div>}
          <pre className="mt-2 overflow-x-auto rounded bg-[#111827] p-2 text-[10px] text-slate-300">{block.sympy_code}</pre>
        </div>
      ))}
      {data?.status === 'no_equations_found' && (
        <p className="text-xs text-yellow-400">No equations detected. Paste paper text with $...$ or equation blocks.</p>
      )}
    </div>
  );
}

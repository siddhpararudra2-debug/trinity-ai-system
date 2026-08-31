import React, { useState } from 'react';
import { useRunMathEngine, useRunQuantumEngine, useRunMakerCad, useRunMakerPcb, useRunLiteratureEngine } from '@workspace/api-client-react';

export function EngineTestDialog({ engine, children }: { engine: any, children: React.ReactNode }) {
  const [open, setOpen] = useState(false);
  const [input, setInput] = useState('');
  const [result, setResult] = useState<any>(null);

  const math = useRunMathEngine();
  const quantum = useRunQuantumEngine();
  const cad = useRunMakerCad();
  const pcb = useRunMakerPcb();
  const lit = useRunLiteratureEngine();

  const handleTest = async () => {
    setResult(null);
    try {
      let res;
      if (engine.id === 'math') {
        res = await math.mutateAsync({ data: { expression: input } });
      } else if (engine.id === 'quantum') {
        res = await quantum.mutateAsync({ data: { circuit_description: input } });
      } else if (engine.id === 'maker_cad') {
        res = await cad.mutateAsync({ data: { description: input } });
      } else if (engine.id === 'maker_pcb') {
        res = await pcb.mutateAsync({ data: { description: input } });
      } else if (engine.id === 'literature') {
        res = await lit.mutateAsync({ data: { query: input } });
      } else {
        throw new Error(`Direct testing not implemented for engine: ${engine.id}`);
      }
      setResult(res);
    } catch (e: any) {
      setResult({ error: e.message || 'Error occurred' });
    }
  };

  const isPending = math.isPending || quantum.isPending || cad.isPending || pcb.isPending || lit.isPending;

  return (
    <>
      <div onClick={() => setOpen(true)}>{children}</div>
      
      {open && (
        <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/80 backdrop-blur-sm p-4 animate-in fade-in duration-200">
          <div className="bg-sidebar border border-border rounded-lg shadow-2xl w-full max-w-2xl font-mono overflow-hidden flex flex-col max-h-[90vh]">
            <div className="flex items-center justify-between p-4 border-b border-border bg-black/20">
              <div className="flex items-center gap-3">
                <span className="bg-primary/20 text-primary px-2 py-0.5 rounded text-[10px] tracking-widest font-bold">DIAGNOSTIC</span>
                <h2 className="text-primary font-bold tracking-widest">{engine.name.toUpperCase()}</h2>
              </div>
              <button 
                onClick={() => setOpen(false)}
                className="text-muted-foreground hover:text-foreground text-xl leading-none w-8 h-8 flex items-center justify-center rounded hover:bg-white/5 transition-colors"
              >
                &times;
              </button>
            </div>
            
            <div className="p-6 flex flex-col gap-6 overflow-y-auto flex-1">
              <p className="text-sm text-muted-foreground border-l-2 border-primary/50 pl-3 py-1">{engine.description}</p>
              
              <div className="flex flex-col gap-2">
                <label className="text-xs text-primary uppercase tracking-widest">TEST INPUT VECTOR</label>
                <textarea 
                  placeholder="Enter raw input string..." 
                  value={input} 
                  onChange={(e) => setInput(e.target.value)}
                  className="bg-background border border-border rounded p-4 font-mono text-sm min-h-[100px] focus:outline-none focus:border-primary/50 text-foreground placeholder:text-muted-foreground/30 resize-y"
                />
              </div>
              
              <button 
                onClick={handleTest} 
                disabled={!input.trim() || isPending} 
                className="bg-primary text-primary-foreground font-bold text-sm tracking-widest py-3 rounded disabled:opacity-50 disabled:cursor-not-allowed hover:bg-primary/90 transition-colors flex justify-center items-center gap-2"
              >
                {isPending ? (
                  <><span className="w-4 h-4 border-2 border-primary-foreground/30 border-t-primary-foreground rounded-full animate-spin"></span> EXECUTING...</>
                ) : 'EXECUTE'}
              </button>

              {result && (
                <div className="mt-2 flex flex-col gap-2 animate-in fade-in slide-in-from-bottom-2 duration-300">
                  <label className="text-xs text-accent uppercase tracking-widest">OUTPUT RESPONSE</label>
                  <div className="p-4 bg-background rounded border border-border overflow-x-auto max-h-[300px] overflow-y-auto">
                    <pre className="text-xs text-accent whitespace-pre-wrap font-mono">
                      {JSON.stringify(result, null, 2)}
                    </pre>
                  </div>
                </div>
              )}
            </div>
          </div>
        </div>
      )}
    </>
  );
}
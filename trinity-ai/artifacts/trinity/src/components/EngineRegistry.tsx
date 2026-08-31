import React from 'react';
import { useListEngines } from '@workspace/api-client-react';
import { Cpu } from 'lucide-react';
import { EngineTestDialog } from './EngineTestDialog';

export function EngineRegistry() {
  const { data: engines } = useListEngines();
  
  return (
    <div className="flex flex-col gap-3 p-4 border-t border-border">
      <h3 className="text-[10px] font-mono font-bold text-muted-foreground uppercase tracking-widest flex items-center gap-2">
        <Cpu className="w-3 h-3" /> CORE ENGINES
      </h3>
      <div className="flex flex-col gap-1">
        {engines?.map(engine => (
          <EngineTestDialog key={engine.id} engine={engine}>
            <button className="flex items-center justify-between w-full text-left px-3 py-2 rounded hover:bg-white/5 transition-colors group cursor-pointer">
              <div className="flex items-center gap-3">
                <span className={`w-1.5 h-1.5 rounded-full ${engine.status === 'ready' ? 'bg-primary shadow-[0_0_8px_hsl(var(--primary))]' : 'bg-muted-foreground'}`} />
                <span className="text-xs font-mono font-bold text-foreground/80 tracking-wide group-hover:text-foreground transition-colors">{engine.name}</span>
              </div>
              <span className="text-[9px] text-primary/50 opacity-0 group-hover:opacity-100 font-bold tracking-widest border border-primary/20 px-1.5 py-0.5 rounded">TEST</span>
            </button>
          </EngineTestDialog>
        ))}
      </div>
    </div>
  );
}
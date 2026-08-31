import React from 'react';

export function MakerMessage({ data }: { data: any }) {
  const handleDownload = (content: string, defaultName: string) => {
    const blob = new Blob([content], { type: 'text/plain' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = data.filename ? `${data.filename}${defaultName.substring(defaultName.indexOf('.'))}` : defaultName;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
  };

  return (
    <div className="flex flex-col gap-3 font-mono text-sm">
      {data?.description && <p className="text-foreground/80">{data.description}</p>}
      {data?.instructions && <p className="text-muted-foreground border-l-2 border-primary pl-3 py-1 bg-primary/5">{data.instructions}</p>}
      
      {data.script && (
        <div className="relative group border border-white/10 rounded overflow-hidden mt-2">
          <div className="flex items-center justify-between bg-black/50 px-4 py-2 border-b border-white/10">
            <span className="text-xs text-muted-foreground">{data.filename ? `${data.filename}.py` : 'generated-script.py'}</span>
            <button 
              onClick={() => handleDownload(data.script, 'generated-script.py')}
              className="text-xs bg-primary/20 text-primary hover:bg-primary hover:text-primary-foreground px-3 py-1 rounded transition-colors tracking-widest font-bold"
            >
              DOWNLOAD SCRIPT
            </button>
          </div>
          <pre className="p-4 bg-black/30 overflow-x-auto text-xs text-foreground/80 max-h-96 overflow-y-auto scrollbar-hide">
            {data.script}
          </pre>
        </div>
      )}

      {data.sch_content && (
        <div className="relative group border border-white/10 rounded overflow-hidden mt-2">
          <div className="flex items-center justify-between bg-black/50 px-4 py-2 border-b border-white/10">
            <span className="text-xs text-muted-foreground">{data.filename ? `${data.filename}.kicad_sch` : 'schematic.kicad_sch'}</span>
            <button 
              onClick={() => handleDownload(data.sch_content, 'schematic.kicad_sch')}
              className="text-xs bg-primary/20 text-primary hover:bg-primary hover:text-primary-foreground px-3 py-1 rounded transition-colors tracking-widest font-bold"
            >
              DOWNLOAD SCHEMATIC
            </button>
          </div>
          <pre className="p-4 bg-black/30 overflow-x-auto text-xs text-foreground/80 max-h-96 overflow-y-auto scrollbar-hide">
            {data.sch_content}
          </pre>
        </div>
      )}

      {data.pcb_content && (
        <div className="relative group border border-white/10 rounded overflow-hidden mt-2">
          <div className="flex items-center justify-between bg-black/50 px-4 py-2 border-b border-white/10">
            <span className="text-xs text-muted-foreground">{data.filename ? `${data.filename}.kicad_pcb` : 'layout.kicad_pcb'}</span>
            <button 
              onClick={() => handleDownload(data.pcb_content, 'layout.kicad_pcb')}
              className="text-xs bg-primary/20 text-primary hover:bg-primary hover:text-primary-foreground px-3 py-1 rounded transition-colors tracking-widest font-bold"
            >
              DOWNLOAD PCB LAYOUT
            </button>
          </div>
          <pre className="p-4 bg-black/30 overflow-x-auto text-xs text-foreground/80 max-h-96 overflow-y-auto scrollbar-hide">
            {data.pcb_content}
          </pre>
        </div>
      )}
    </div>
  );
}
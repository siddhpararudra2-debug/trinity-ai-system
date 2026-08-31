import React from 'react';

export function LiteratureMessage({ data }: { data: any }) {
  return (
    <div className="flex flex-col gap-4 font-mono text-sm">
      {data?.summary && <p className="text-foreground/90 leading-relaxed border-l-2 border-accent pl-4 py-1">{data.summary}</p>}
      
      {data?.papers && data.papers.length > 0 && (
        <div className="flex flex-col gap-3 mt-2">
          <div className="text-xs text-primary uppercase">CITED LITERATURE:</div>
          {data.papers.map((paper: any, i: number) => (
            <a 
              key={i} 
              href={paper.url} 
              target="_blank" 
              rel="noopener noreferrer" 
              className="block p-4 bg-black/30 border border-white/5 rounded hover:border-primary/50 transition-colors group"
            >
              <h4 className="font-semibold text-accent mb-1 group-hover:text-primary transition-colors">{paper.title}</h4>
              <div className="text-xs text-muted-foreground mb-3 flex items-center gap-2 flex-wrap">
                {paper.authors?.join(', ')}
                {paper.published && <span className="opacity-50">({paper.published})</span>}
              </div>
              <p className="text-xs line-clamp-3 text-foreground/60 leading-relaxed">{paper.abstract}</p>
              <div className="mt-3 text-[10px] text-primary opacity-0 group-hover:opacity-100 transition-opacity flex items-center gap-1 tracking-widest">
                ACCESS FULL TEXT &gt;
              </div>
            </a>
          ))}
        </div>
      )}
    </div>
  );
}
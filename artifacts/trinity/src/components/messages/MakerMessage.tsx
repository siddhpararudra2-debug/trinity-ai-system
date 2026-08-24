import React from 'react';

type Artifact = {
  id: string;
  kind: string;
  filename: string;
  mime_type: string;
  size_bytes: number;
  download_url: string;
};

function formatBytes(value: number) {
  if (value < 1024) return `${value} B`;
  if (value < 1024 * 1024) return `${(value / 1024).toFixed(1)} KB`;
  return `${(value / (1024 * 1024)).toFixed(1)} MB`;
}

export function MakerMessage({ data }: { data: any }) {
  const job = data?.job_id ? data : data?.job;
  const artifacts: Artifact[] = job?.artifacts || [];

  if (job) {
    const checks = job.validation?.checks || [];
    return (
      <div className="flex flex-col gap-4 font-mono text-sm">
        <div className="flex flex-wrap items-center gap-2">
          <span className="text-primary font-bold uppercase tracking-widest">{job.engine?.replace('_', ' ')}</span>
          <span className="text-[10px] px-2 py-1 rounded border border-primary/30 text-primary">{job.status}</span>
          <span className="text-[10px] px-2 py-1 rounded border border-border text-muted-foreground">validation: {job.validation?.status || 'not_run'}</span>
        </div>
        <p className="text-muted-foreground text-xs">JOB_ID: {job.job_id}</p>
        {job.questions?.length > 0 && (
          <div className="border-l-2 border-yellow-500/70 bg-yellow-500/5 p-3">
            <div className="text-yellow-400 text-xs uppercase tracking-widest mb-2">CONFIRMATION NEEDED</div>
            {job.questions.map((question: string) => <div key={question} className="text-xs text-foreground/80">• {question}</div>)}
          </div>
        )}
        {job.assumptions?.length > 0 && (
          <div className="border-l-2 border-primary/50 bg-primary/5 p-3">
            <div className="text-primary text-xs uppercase tracking-widest mb-2">ASSUMPTIONS</div>
            {job.assumptions.map((assumption: string) => <div key={assumption} className="text-xs text-muted-foreground">• {assumption}</div>)}
          </div>
        )}
        {artifacts.length > 0 && (
          <div className="flex flex-col gap-2">
            <div className="text-xs text-primary uppercase tracking-widest">GENERATED ARTIFACTS</div>
            {artifacts.map((artifact) => (
              <a key={artifact.id} href={artifact.download_url} download={artifact.filename} className="flex items-center justify-between gap-4 p-3 bg-black/30 border border-white/10 rounded hover:border-primary/60 transition-colors">
                <span className="truncate text-foreground/90">{artifact.filename}</span>
                <span className="shrink-0 text-[10px] text-primary">DOWNLOAD · {formatBytes(artifact.size_bytes)}</span>
              </a>
            ))}
          </div>
        )}
        {checks.filter((check: any) => check.status !== 'passed').length > 0 && (
          <div className="border border-border rounded p-3">
            <div className="text-xs text-accent uppercase tracking-widest mb-2">VALIDATION NOTES</div>
            {checks.filter((check: any) => check.status !== 'passed').map((check: any) => (
              <div key={check.name} className="text-xs text-muted-foreground mb-1">[{check.status}] {check.message}</div>
            ))}
          </div>
        )}
      </div>
    );
  }

  const legacyArtifacts = [
    data?.script ? { filename: data.filename || 'fusion_script.py', content: data.script } : null,
    data?.sch_content ? { filename: `${data.filename || 'trinity_board'}.kicad_sch`, content: data.sch_content } : null,
    data?.pcb_content ? { filename: `${data.filename || 'trinity_board'}.kicad_pcb`, content: data.pcb_content } : null,
  ].filter(Boolean) as { filename: string; content: string }[];

  const handleDownload = (artifact: { filename: string; content: string }) => {
    const blob = new Blob([artifact.content], { type: 'text/plain' });
    const url = URL.createObjectURL(blob);
    const anchor = document.createElement('a');
    anchor.href = url;
    anchor.download = artifact.filename;
    document.body.appendChild(anchor);
    anchor.click();
    anchor.remove();
    URL.revokeObjectURL(url);
  };

  return (
    <div className="flex flex-col gap-3 font-mono text-sm">
      {data?.description && <p className="text-foreground/80">{data.description}</p>}
      {data?.instructions && <p className="text-muted-foreground border-l-2 border-primary pl-3 py-1 bg-primary/5">{data.instructions}</p>}
      {legacyArtifacts.map((artifact) => (
        <div key={artifact.filename} className="flex items-center justify-between gap-4 border border-white/10 rounded p-3 bg-black/30">
          <span className="text-xs text-muted-foreground truncate">{artifact.filename}</span>
          <button onClick={() => handleDownload(artifact)} className="text-xs bg-primary/20 text-primary hover:bg-primary hover:text-primary-foreground px-3 py-1 rounded transition-colors tracking-widest font-bold">DOWNLOAD</button>
        </div>
      ))}
    </div>
  );
}

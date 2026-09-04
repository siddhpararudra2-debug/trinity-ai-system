import React, { useEffect, useState } from 'react';
import { getAuthHeaders } from '@/lib/auth';

type Props = {
  jobId?: string;
};

export function PcbSvgPreview({ jobId }: Props) {
  const [svg, setSvg] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    const id = jobId || 'demo';
    fetch(`/api/preview/pcb-svg?job_id=${encodeURIComponent(id)}`, { headers: getAuthHeaders() })
      .then(async (response) => {
        if (!response.ok) throw new Error(`Preview failed (${response.status})`);
        const body = await response.json();
        setSvg(body.svg);
      })
      .catch((err) => setError(String(err)));
  }, [jobId]);

  if (error) {
    return <div className="rounded border border-red-500/30 bg-red-500/5 p-3 text-xs text-red-300">{error}</div>;
  }
  if (!svg) {
    return <div className="rounded border border-white/10 bg-black/20 p-4 text-xs text-muted-foreground">Loading schematic preview…</div>;
  }

  return (
    <div className="overflow-hidden rounded border border-white/10 bg-[#0b1220]">
      <div className="border-b border-white/10 px-3 py-2 text-[10px] uppercase tracking-widest text-primary">PCB Schematic Preview</div>
      <div className="p-2" dangerouslySetInnerHTML={{ __html: svg }} />
    </div>
  );
}

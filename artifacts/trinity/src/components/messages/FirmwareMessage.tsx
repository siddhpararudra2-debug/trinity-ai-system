import React, { useMemo, useState } from 'react';
import { Check, Clipboard, Download, FileArchive, Maximize2, Minimize2 } from 'lucide-react';
import { getAuthHeaders } from '@/lib/auth';

type FirmwareFile = { path: string; content: string; language?: string };
type FirmwareArtifact = { id: string; filename: string; download_url: string; kind?: string; size_bytes?: number };

type FirmwareJob = {
  job_id: string;
  status: string;
  detected_language?: string;
  rendering_hint?: string;
  files?: FirmwareFile[];
  artifacts?: FirmwareArtifact[];
  resource_estimate?: { flash_bytes: number; ram_bytes: number; cpu_percent: number; flash_percent?: number; ram_percent?: number; confidence?: number; assumptions?: string[] };
  dependencies?: { name: string; verified: boolean; reason: string }[];
  security_findings?: { rule: string; severity: string; message: string; line?: number; remediation?: string }[];
  validation?: { status: string; checks?: { name: string; status: string; message: string; line?: number; severity?: string }[] };
};

function escapeHtml(value: string) {
  return value.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;').replace(/'/g, '&#039;');
}

function highlight(code: string) {
  const pattern = /(\/\/[^\n]*|#[^\n]*|\/\*[\s\S]*?\*\/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|\b\d+(?:\.\d+)?\b|\b(?:const|static|volatile|int|char|void|return|if|else|for|while|struct|class|fn|let|pub|use|mod|module|entity|architecture|assign|always|import|from|def|async|await|include|define|true|false)\b)/g;
  return code.split(pattern).filter(Boolean).map((part, index) => {
    const escaped = escapeHtml(part);
    if (/^\/\//.test(part) || /^#/.test(part) || /^\/\*/.test(part)) return <span key={index} className="text-slate-500">{escaped}</span>;
    if (/^["']/.test(part)) return <span key={index} className="text-amber-300">{escaped}</span>;
    if (/^\d/.test(part)) return <span key={index} className="text-cyan-300">{escaped}</span>;
    if (/^(const|static|volatile|int|char|void|return|if|else|for|while|struct|class|fn|let|pub|use|mod|module|entity|architecture|assign|always|import|from|def|async|await|include|define|true|false)$/.test(part)) return <span key={index} className="text-fuchsia-300">{escaped}</span>;
    return <React.Fragment key={index}>{escaped}</React.Fragment>;
  });
}

function downloadBlob(blob: Blob, filename: string) {
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement('a');
  anchor.href = url;
  anchor.download = filename;
  document.body.appendChild(anchor);
  anchor.click();
  anchor.remove();
  URL.revokeObjectURL(url);
}

async function downloadProtected(url: string, filename: string) {
  const response = await fetch(url, { headers: getAuthHeaders() });
  if (!response.ok) throw new Error(`Download failed (${response.status})`);
  downloadBlob(await response.blob(), filename);
}

function ResourceBar({ label, value, percent }: { label: string; value: string; percent?: number }) {
  const safePercent = Math.max(0, Math.min(100, percent ?? 0));
  const color = safePercent >= 90 ? 'bg-red-500' : safePercent >= 70 ? 'bg-amber-400' : 'bg-emerald-400';
  return <div className="space-y-1"><div className="flex justify-between text-[10px] uppercase tracking-wider text-muted-foreground"><span>{label}</span><span>{value}</span></div><div className="h-1.5 overflow-hidden rounded bg-white/10"><div className={`h-full ${color}`} style={{ width: `${safePercent}%` }} /></div></div>;
}

export function FirmwareMessage({ data }: { data: FirmwareJob }) {
  const [activePath, setActivePath] = useState(data.files?.[0]?.path || '');
  const [copied, setCopied] = useState(false);
  const [expanded, setExpanded] = useState(false);
  const files = data.files || [];
  const active = useMemo(() => files.find((file) => file.path === activePath) || files[0], [files, activePath]);
  const archive = data.artifacts?.find((artifact) => artifact.kind === 'firmware_project_bundle' || artifact.filename.endsWith('.zip'));
  const findings = [...(data.security_findings || []), ...(data.validation?.checks || []).filter((check) => check.status !== 'passed')];

  const copy = async () => {
    if (!active) return;
    await navigator.clipboard.writeText(active.content);
    setCopied(true);
    window.setTimeout(() => setCopied(false), 1400);
  };

  return <div className="flex flex-col gap-4 font-mono text-sm">
    <div className="flex flex-wrap items-center gap-2"><span className="font-bold uppercase tracking-widest text-primary">UNIVERSAL FIRMWARE</span><span className="rounded border border-primary/30 px-2 py-1 text-[10px] text-primary">{data.status}</span><span className="rounded border border-border px-2 py-1 text-[10px] text-muted-foreground">{data.detected_language || 'multi-file'}</span></div>
    <p className="text-xs text-muted-foreground">JOB_ID: {data.job_id}</p>
    {files.length > 0 && <section className="overflow-hidden rounded border border-white/10 bg-[#111827]">
      <div className="flex flex-wrap items-center gap-2 border-b border-white/10 bg-white/5 p-2"><div className="flex min-w-0 flex-1 gap-1 overflow-x-auto">{files.map((file) => <button key={file.path} onClick={() => setActivePath(file.path)} className={`whitespace-nowrap rounded px-2 py-1 text-[10px] ${active?.path === file.path ? 'bg-primary/20 text-primary' : 'text-muted-foreground hover:bg-white/10'}`}>{file.path}</button>)}</div><div className="flex gap-1"><button title="Copy current file" onClick={copy} className="rounded p-1.5 text-muted-foreground hover:bg-white/10 hover:text-primary">{copied ? <Check className="h-3.5 w-3.5 text-emerald-400" /> : <Clipboard className="h-3.5 w-3.5" />}</button>{active && <button title="Download current file" onClick={() => downloadBlob(new Blob([active.content], { type: 'text/plain' }), active.path.split('/').pop() || 'firmware.txt')} className="rounded p-1.5 text-muted-foreground hover:bg-white/10 hover:text-primary"><Download className="h-3.5 w-3.5" /></button>}{archive && <button title="Download project ZIP" onClick={() => downloadProtected(archive.download_url, archive.filename)} className="rounded p-1.5 text-muted-foreground hover:bg-white/10 hover:text-primary"><FileArchive className="h-3.5 w-3.5" /></button>}<button title={expanded ? 'Collapse code' : 'Expand code'} onClick={() => setExpanded(!expanded)} className="rounded p-1.5 text-muted-foreground hover:bg-white/10 hover:text-primary">{expanded ? <Minimize2 className="h-3.5 w-3.5" /> : <Maximize2 className="h-3.5 w-3.5" />}</button></div></div>
      {active && <div className={`overflow-auto ${expanded ? 'max-h-[70vh]' : 'max-h-96'}`}><table className="w-full border-collapse text-left text-xs leading-5"><tbody>{active.content.split('\n').map((line, index) => <tr key={index} className="hover:bg-white/5"><td className="select-none border-r border-white/10 px-3 text-right text-slate-600">{index + 1}</td><td className="whitespace-pre px-4 py-0.5">{highlight(line)}</td></tr>)}</tbody></table></div>}
    </section>}
    {data.resource_estimate && <section className="grid gap-3 rounded border border-white/10 bg-black/20 p-3 sm:grid-cols-3"><ResourceBar label="Flash" value={`${data.resource_estimate.flash_bytes} B`} percent={data.resource_estimate.flash_percent} /><ResourceBar label="RAM" value={`${data.resource_estimate.ram_bytes} B`} percent={data.resource_estimate.ram_percent} /><ResourceBar label="CPU" value={`${data.resource_estimate.cpu_percent}%`} percent={data.resource_estimate.cpu_percent} /></section>}
    {data.dependencies && data.dependencies.length > 0 && <section className="rounded border border-white/10 p-3"><div className="mb-2 text-[10px] uppercase tracking-widest text-primary">DEPENDENCIES</div>{data.dependencies.map((dependency) => <div key={dependency.name} className="flex gap-2 text-xs"><span className={dependency.verified ? 'text-emerald-400' : 'text-amber-400'}>{dependency.verified ? 'VERIFIED' : 'REVIEW'}</span><span>{dependency.name}</span><span className="text-muted-foreground">— {dependency.reason}</span></div>)}</section>}
    {findings.length > 0 && <details open className="rounded border border-amber-500/40 bg-amber-500/5 p-3"><summary className="cursor-pointer text-xs uppercase tracking-widest text-amber-300">VALIDATION AND SECURITY FINDINGS ({findings.length})</summary><div className="mt-2 space-y-2">{findings.map((finding: any, index) => <div key={`${finding.name || finding.rule}-${index}`} className="text-xs text-muted-foreground"><span className="mr-2 text-amber-300">[{finding.severity || finding.status || 'warning'}]</span>{finding.line ? `line ${finding.line}: ` : ''}{finding.message}{finding.remediation ? <span className="block pl-4 text-foreground/70">Fix: {finding.remediation}</span> : null}</div>)}</div></details>}
  </div>;
}

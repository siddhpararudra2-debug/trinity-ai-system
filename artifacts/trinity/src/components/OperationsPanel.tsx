import { useEffect, useState } from 'react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';

const TOKEN_KEY = 'trinity_access_token';
type Job = { id: string; kind: string; status: string; attempts: number; error?: string | null };
type Health = { status?: string; capabilities?: Record<string, boolean> };

function headers(): HeadersInit {
  const token = localStorage.getItem(TOKEN_KEY);
  return token ? { Authorization: `Bearer ${token}` } : {};
}

export function OperationsPanel() {
  const [health, setHealth] = useState<Health | null>(null);
  const [jobs, setJobs] = useState<Job[]>([]);

  async function refresh() {
    const [healthResponse, jobsResponse] = await Promise.all([
      fetch('/api/healthz'),
      fetch('/api/jobs', { headers: headers() }),
    ]);
    if (healthResponse.ok) setHealth(await healthResponse.json());
    if (jobsResponse.ok) setJobs(await jobsResponse.json());
  }

  useEffect(() => {
    void refresh();
    const timer = window.setInterval(() => void refresh(), 5000);
    return () => window.clearInterval(timer);
  }, []);

  async function cancelJob(id: string) {
    await fetch(`/api/jobs/${id}/cancel`, { method: 'POST', headers: headers() });
    await refresh();
  }

  return (
    <Card className="w-80 border-border/70 bg-card/95 shadow-xl backdrop-blur">
      <CardHeader className="p-4 pb-2"><CardTitle className="text-sm">Operations</CardTitle></CardHeader>
      <CardContent className="space-y-3 p-4 pt-2 text-xs">
        <div className="flex items-center justify-between">
          <span>API health</span><span className={health?.status === 'ok' ? 'text-primary' : 'text-muted-foreground'}>{health?.status || 'checking'}</span>
        </div>
        {health?.capabilities && <div className="grid grid-cols-2 gap-1 text-muted-foreground">
          {Object.entries(health.capabilities).map(([name, ready]) => <span key={name}>{name}: {ready ? 'ready' : 'unavailable'}</span>)}
        </div>}
        <div className="border-t border-border/60 pt-2">
          <div className="mb-1 text-muted-foreground">Recent durable jobs</div>
          {jobs.length === 0 && <div className="text-muted-foreground">No queued jobs</div>}
          {jobs.slice(0, 5).map((job) => <div className="flex items-center justify-between gap-2 py-1" key={job.id}>
            <span className="truncate">{job.kind} · {job.status}</span>
            {!['succeeded', 'failed', 'cancelled'].includes(job.status) && <Button size="sm" variant="ghost" onClick={() => void cancelJob(job.id)}>Cancel</Button>}
          </div>)}
        </div>
      </CardContent>
    </Card>
  );
}

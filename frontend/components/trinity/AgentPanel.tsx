'use client';

import { useCallback, useEffect, useRef, useState } from 'react';
import {
  getArtifactUrl,
  runAgent,
  type AgentRunOut,
} from '@/lib/api/trinity';

/**
 * Agent workbench — the coder/researcher multi-step engine loop.
 *
 * The refusal path is the product: without a model endpoint the API
 * answers `capability_unavailable` and this panel renders that refusal
 * verbatim (code + message + the attach env var) — never a synthetic
 * run, never a fake step list. When a model IS attached, every step is
 * shown as the loop actually saw it: registry-advertised engine calls
 * with their true success, real errors, and real artifact links.
 */
export default function AgentPanel() {
  const [role, setRole] = useState('coder');
  const [prompt, setPrompt] = useState('');
  const [busy, setBusy] = useState(false);
  const [outcome, setOutcome] = useState<AgentRunOut | null>(null);
  const [errorNote, setErrorNote] = useState<string | null>(null);
  const streamRef = useRef<HTMLDivElement>(null);

  const run = useCallback(async () => {
    if (busy || !prompt.trim()) return;
    setBusy(true);
    setOutcome(null);
    setErrorNote(null);
    try {
      setOutcome(await runAgent(role, prompt.trim()));
    } catch (err) {
      setErrorNote(err instanceof Error ? err.message : String(err));
    } finally {
      setBusy(false);
    }
  }, [busy, prompt, role]);

  const errors = outcome?.errors ?? [];
  const firstError = errors[0];
  const refusedNoModel =
    firstError?.code === 'capability_unavailable' ||
    /no model/i.test(firstError?.message ?? '');

  const statusLabel = () => {
    if (errorNote) return 'AGENT UNREACHABLE — API CALL FAILED';
    if (!outcome) return null;
    if (refusedNoModel) return 'AGENT REFUSED — NO MODEL ATTACHED';
    if (outcome.success) return `AGENT COMPLETED — ${outcome.steps?.length ?? 0} steps`;
    if (outcome.status === 'step_budget_exhausted') {
      return `AGENT STOPPED — STEP BUDGET EXHAUSTED (${outcome.steps?.length ?? 0} steps)`;
    }
    return `AGENT FAILED${firstError?.code ? ` (${firstError.code})` : ''}`;
  };

  const status = statusLabel();
  useEffect(() => {
    if (streamRef.current) {
      streamRef.current.scrollTop = streamRef.current.scrollHeight;
    }
  }, [outcome]);

  return (
    <section
      className="agent-panel rounded-xl border border-zinc-800 bg-zinc-950/60 p-4"
      data-testid="agent-panel"
    >
      <header className="mb-3 flex items-center justify-between">
        <h3 className="text-sm font-semibold tracking-wide text-zinc-200">
          AGENT WORKBENCH
        </h3>
        <select
          className="agent-role-select rounded border border-zinc-700 bg-zinc-900 px-2 py-1 text-xs text-zinc-300"
          value={role}
          disabled={busy}
          onChange={(e) => setRole(e.target.value)}
          aria-label="agent role"
        >
          <option value="coder">coder</option>
          <option value="researcher">researcher</option>
        </select>
      </header>

      <div className="mb-2 flex gap-2">
        <textarea
          className="agent-prompt-input min-h-[64px] flex-1 rounded border border-zinc-700 bg-zinc-900 px-2 py-1 font-mono text-xs text-zinc-200 placeholder:text-zinc-600"
          placeholder="describe the engineering task for the agent…"
          value={prompt}
          disabled={busy}
          onChange={(e) => setPrompt(e.target.value)}
        />
        <button
          type="button"
          className="agent-run-btn rounded border border-sky-500/70 bg-sky-500/10 px-3 py-1 text-xs font-semibold text-sky-300 hover:bg-sky-500/20 disabled:opacity-40"
          disabled={busy || !prompt.trim()}
          onClick={run}
        >
          {busy ? 'RUNNING…' : 'RUN AGENT'}
        </button>
      </div>

      {status && (
        <p className="agent-status mb-2 font-mono text-[11px] font-semibold tracking-wider text-zinc-300">
          {status}
        </p>
      )}

      {errorNote && (
        <p className="agent-error mb-2 rounded border border-red-500/50 bg-red-500/10 px-2 py-1 text-[11px] text-red-300">
          {errorNote}
        </p>
      )}

      {refusedNoModel && firstError && (
        <p className="agent-refusal mb-2 rounded border border-amber-500/40 bg-amber-500/5 px-2 py-1 font-mono text-[11px] text-amber-300">
          {firstError.code}
          {typeof firstError.details === 'object' && firstError.details !== null
            ? ` env_var=${
                (firstError.details as Record<string, unknown>).env_var ?? ''
              }`
            : ''}
          <span className="agent-refusal-message block text-zinc-400">
            {firstError.message}
          </span>
        </p>
      )}

      {outcome?.summary && (
        <p className="agent-summary mb-2 rounded border border-emerald-500/40 bg-emerald-500/5 px-2 py-1 text-[11px] text-emerald-300">
          {outcome.summary}
        </p>
      )}

      <div ref={streamRef} className="max-h-80 overflow-y-auto">
        {(outcome?.steps ?? []).map((step) => (
          <div
            key={step.step}
            className="agent-step rounded border border-zinc-800 bg-zinc-900/40 px-2 py-1 mb-1 font-mono text-[11px]"
          >
            <span className="text-zinc-500">step {step.step} · </span>
            {step.action === 'call' ? (
              <>
                <span className="text-zinc-300">
                  call {step.engine} · {step.operation}
                </span>
                {step.success ? (
                  <span className="ml-2 text-emerald-400">OK</span>
                ) : (
                  <span className="ml-2 text-red-400">
                    FAILED
                    {step.errors?.[0]
                      ? ` — ${step.errors[0].message}`
                      : ''}
                  </span>
                )}
                {(step.artifacts ?? []).map((a) => (
                  <a
                    key={a.artifact_id}
                    href={getArtifactUrl(a.artifact_id)}
                    className="agent-artifact ml-2 rounded border border-sky-500/60 bg-sky-500/10 px-1.5 py-0.5 text-[10px] text-sky-300"
                  >
                    {a.type}
                  </a>
                ))}
              </>
            ) : (
              <span className="text-amber-400">
                {step.action}
                {step.error ? ` — ${step.error}` : ''}
              </span>
            )}
          </div>
        ))}
      </div>
    </section>
  );
}

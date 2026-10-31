"use client";

import { useCallback, useEffect, useRef, useState } from "react";
import { streamPlan, TRINITY_API_URL } from "@/lib/api/trinity";

/**
 * DAG execution pipeline visualization (Stage 4; planned like a pipeline in M15).
 *
 * Submits a {id, engine, operation, parameters, dependencies} workflow to
 * POST /api/workflows/execute and renders every node with its truthful
 * status — completed / failed / queued (blocked by an upstream failure).
 * The PLAN affordance streams a natural-language request to
 * /api/intelligence/stream-plan; its final frame carries validated nodes
 * in this component's exact spec shape, loaded for an explicit EXECUTE —
 * never auto-run. Delta tokens render raw while the model thinks; the
 * plan's source is shown verbatim (model, or the labeled deterministic
 * router when no model endpoint is configured).
 * No node is ever shown as "done" that did not complete server-side.
 */

export type WorkflowNodeSpec = {
  id: string;
  engine: string;
  operation: string;
  parameters?: Record<string, unknown>;
  dependencies?: string[];
};

export type WorkflowNodeOutcome = {
  id: string;
  status: string;
  job_id: string;
  result: Record<string, unknown>;
  artifacts: unknown[];
  errors: { code: string; message: string }[];
};

export type WorkflowRunOutcome = {
  success: boolean;
  nodes: WorkflowNodeOutcome[];
  errors: { code: string; message: string }[];
};

export const NODE_STATUS_LABELS: Record<string, string> = {
  completed: "COMPLETED",
  failed: "FAILED",
  queued: "BLOCKED",
};

const STATUS_STYLES: Record<string, string> = {
  completed: "border-emerald-500/60 bg-emerald-500/10 text-emerald-300",
  failed: "border-rose-500/60 bg-rose-500/10 text-rose-300",
  queued: "border-zinc-500/60 bg-zinc-500/10 text-zinc-400",
};

export function statusStyle(status: string): string {
  return (
    STATUS_STYLES[status] ??
    "border-zinc-600 bg-zinc-900/60 text-zinc-200"
  );
}

/** Concise engine label for a chip. */
export function nodeTitle(node: WorkflowNodeSpec): string {
  return `${node.engine} · ${node.operation}`;
}

/**
 * Edge stroke: done→done solid green, failed edges red, queued grey.
 * Pure function so it renders without any state.
 */
export function edgeClass(from: WorkflowNodeOutcome | undefined, to: WorkflowNodeOutcome | undefined): string {
  if (!from || !to) return "stroke-zinc-600";
  if (from.status === "failed" && to.status === "queued") return "stroke-zinc-700 stroke-dasharray-4";
  if (from.status === "completed" && to.status === "completed") return "stroke-emerald-500";
  if (from.status === "failed" || to.status === "failed") return "stroke-rose-500";
  return "stroke-zinc-600";
}

async function runWorkflow(
  nodes: WorkflowNodeSpec[],
  signal?: AbortSignal,
): Promise<WorkflowRunOutcome> {
  const response = await fetch(`${TRINITY_API_URL}/workflows/execute`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ nodes }),
    signal,
  });
  if (!response.ok) {
    const body = await response.json().catch(() => null);
    const message =
      body?.errors?.[0]?.message ?? `workflow execution failed (${response.status})`;
    throw new Error(message);
  }
  return (await response.json()) as WorkflowRunOutcome;
}

export default function DagRunPanel({
  nodes,
  onNodesChange,
}: {
  nodes: WorkflowNodeSpec[];
  onNodesChange?: (nodes: WorkflowNodeSpec[]) => void;
}) {
  const [runState, setRunState] = useState<{ sig: string; nodes: WorkflowNodeOutcome[] } | null>(
    null,
  );
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [planText, setPlanText] = useState("");
  const [planning, setPlanning] = useState(false);
  const [planStream, setPlanStream] = useState<string | null>(null);
  // {label, summary} — the plan's source and the refusal reason, verbatim.
  const [planNote, setPlanNote] = useState<{ label: string; summary: string } | null>(null);
  const streamRef = useRef<HTMLPreElement>(null);

  const graphSignature = JSON.stringify(nodes);
  // Outcomes from a previous graph shape are stale — never display them.
  const outcomes =
    runState && runState.sig === graphSignature ? runState.nodes : [];

  const run = useCallback(async () => {
    setBusy(true);
    setError(null);
    try {
      const outcome = await runWorkflow(nodes);
      setRunState({ sig: JSON.stringify(nodes), nodes: outcome.nodes });
      if (!outcome.success && outcome.errors.length > 0) {
        setError(outcome.errors[0].message);
      }
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setBusy(false);
    }
  }, [nodes]);

  const plan = useCallback(async () => {
    if (planning || busy || !onNodesChange) return;
    const text = planText.trim();
    if (!text) return;
    setPlanning(true);
    setError(null);
    setPlanNote(null);
    setPlanStream("");
    try {
      const outcome = await streamPlan(text, (delta) => {
        setPlanStream((prev) => (prev ?? "") + delta);
      });
      if (outcome.type === "final") {
        onNodesChange(outcome.nodes);
        const label = outcome.source === "model"
          ? "PLAN FROM MODEL — EDIT THEN EXECUTE"
          : "PLAN FROM DETERMINISTIC ROUTER — NO MODEL ATTACHED";
        setPlanNote({
          label,
          summary: outcome.summary || `${outcome.nodes.length} node(s) loaded, ready to execute`,
        });
      } else {
        setPlanNote({
          label: `PLAN REFUSED (${outcome.code})`,
          summary: outcome.message,
        });
      }
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setPlanning(false);
    }
  }, [planText, planning, busy, onNodesChange]);

  // Streaming deltas scroll to the newest line, plain text, no trimming.
  useEffect(() => {
    if (streamRef.current) streamRef.current.scrollTop = streamRef.current.scrollHeight;
  }, [planStream]);

  const outcomeById = new Map(outcomes.map((o) => [o.id, o]));

  return (
    <section className="rounded-xl border border-zinc-800 bg-zinc-950/60 p-4">
      <header className="mb-3 flex items-center justify-between">
        <h3 className="text-sm font-semibold tracking-wide text-zinc-200">
          DAG EXECUTION PIPELINE
        </h3>
        <div className="flex items-center gap-2">
          {onNodesChange && (
            <button
              type="button"
              className="rounded border border-zinc-700 px-2 py-1 text-xs text-zinc-300 hover:border-zinc-500"
              onClick={() =>
                onNodesChange([
                  ...nodes,
                  {
                    id: `n${nodes.length + 1}`,
                    engine: "math",
                    operation: "evaluate",
                    parameters: { expression: "1+1" },
                    dependencies: nodes.length ? [nodes[nodes.length - 1].id] : [],
                  },
                ])
              }
            >
              + NODE
            </button>
          )}
          <button
            type="button"
            disabled={busy || nodes.length === 0}
            onClick={run}
            className="rounded border border-sky-500/70 bg-sky-500/10 px-3 py-1 text-xs font-semibold text-sky-300 hover:bg-sky-500/20 disabled:opacity-40"
          >
            {busy ? "RUNNING…" : "EXECUTE PIPELINE"}
          </button>
        </div>
      </header>

      {error && <p className="mb-2 text-xs text-rose-400">{error}</p>}
      {nodes.length === 0 && <p className="text-xs text-zinc-500">No nodes yet.</p>}

      {onNodesChange && (
        <div className="mb-3 space-y-2" aria-label="Plan this pipeline">
          <div className="flex gap-2">
            <input
              type="text"
              className="dag-plan-input flex-1 rounded border border-zinc-700 bg-zinc-900 px-2 py-1 text-xs text-zinc-200 placeholder:text-zinc-600 focus:border-sky-600 focus:outline-none"
              value={planText}
              placeholder="Describe a pipeline — e.g. evaluate 2+3, then solve x + that = 10"
              aria-label="Natural language pipeline request"
              onChange={(e) => setPlanText(e.target.value)}
              onKeyDown={(e) => {
                if (e.key === "Enter") plan();
              }}
              disabled={planning || busy}
            />
            <button
              type="button"
              className="dag-plan-btn rounded border border-emerald-500/60 bg-emerald-500/10 px-3 py-1 text-xs font-semibold text-emerald-300 hover:bg-emerald-500/20 disabled:opacity-40"
              disabled={planning || busy || !planText.trim()}
              onClick={plan}
            >
              {planning ? "PLANNING…" : "PLAN"}
            </button>
          </div>
          {planNote && (
            <p className="dag-plan-status text-[11px] text-zinc-400">
              <span className="font-semibold text-zinc-200">{planNote.label}</span>
              {planNote.summary ? ` — ${planNote.summary}` : ""}
            </p>
          )}
          {planStream !== null && (
            <pre
              ref={streamRef}
              className="dag-plan-stream max-h-24 overflow-y-auto rounded border border-zinc-800 bg-black/50 p-2 font-mono text-[10px] leading-snug text-zinc-400 whitespace-pre-wrap"
            >
              {planStream}
            </pre>
          )}
        </div>
      )}

      <ol className="space-y-2">
        {nodes.map((node, index) => {
          const outcome = outcomeById.get(node.id);
          const status = outcome?.status ?? "pending";
          return (
            <li key={node.id} className="flex flex-col gap-1">
              <div
                className={`flex items-center justify-between rounded-lg border px-3 py-2 text-xs ${statusStyle(
                  outcome ? status : "pending",
                )}`}
              >
                <div>
                  <span className="font-mono font-semibold">{node.id}</span>
                  <span className="mx-2 text-zinc-500">→</span>
                  <span>{nodeTitle(node)}</span>
                  {node.dependencies && node.dependencies.length > 0 && (
                    <span className="ml-2 text-zinc-500">
                      depends: {node.dependencies.join(", ")}
                    </span>
                  )}
                </div>
                <div className="flex items-center gap-2">
                  {outcome?.job_id && (
                    <span className="font-mono text-[10px] text-zinc-500">
                      {outcome.job_id.slice(0, 8)}
                    </span>
                  )}
                  <span className="font-semibold tracking-wider">
                    {outcome ? (NODE_STATUS_LABELS[status] ?? status.toUpperCase()) : "PENDING"}
                  </span>
                </div>
              </div>
              {outcome?.errors && outcome.errors.length > 0 && (
                <p className="pl-3 text-[11px] text-rose-400">
                  {outcome.errors[0].message}
                </p>
              )}
              {index < nodes.length - 1 && <span aria-hidden className="pl-4 text-zinc-700">↓</span>}
            </li>
          );
        })}
      </ol>
    </section>
  );
}

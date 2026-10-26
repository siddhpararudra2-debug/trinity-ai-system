"use client";

import { useCallback, useState } from "react";

/**
 * DAG execution pipeline visualization (Stage 4).
 *
 * Submits a {id, engine, operation, parameters, dependencies} workflow to
 * POST /api/workflows/execute and renders every node with its truthful
 * status — completed / failed / queued (blocked by an upstream failure).
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
  const response = await fetch("/api/workflows/execute", {
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

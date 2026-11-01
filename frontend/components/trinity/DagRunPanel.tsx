"use client";

import {
  useCallback,
  useEffect,
  useRef,
  useState,
  type RefObject,
} from "react";
import {
  getArtifactUrl,
  getEngines,
  streamPlan,
  TRINITY_API_URL,
  type EngineInfo,
} from "@/lib/api/trinity";

/**
 * DAG execution pipeline panel (Stage 4; planned via the M15.2 PLAN
 * affordance, productized in M15.3).
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
 * Product surface (when onNodesChange is wired): per-node EDIT — engine
 * and operation selects rostered from the live /engines registry, JSON
 * parameters whose parse failure is shown verbatim, dependency toggles —
 * plus a threaded-ref helper that inserts
 * `{node:<id>}.result.<path>` at the caret (paths observed from the last
 * completed run, never invented), artifact links after a completed run,
 * and a guarded node removal that names the dependents it would orphan
 * instead of breaking the graph silently.
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

/** The node editor's in-progress payload (M15.3). */
type EditorDraft = {
  engine: string;
  operation: string;
  paramsText: string;
  deps: string[];
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

/** Parse the editor's parameters JSON, or name the exact failure. */
export function tryParseParams(
  text: string,
):
  | { ok: true; parameters: Record<string, unknown> }
  | { ok: false; error: string } {
  try {
    const value = JSON.parse(text) as unknown;
    if (!value || typeof value !== "object" || Array.isArray(value)) {
      return { ok: false, error: "parameters must be a JSON object" };
    }
    return { ok: true, parameters: value as Record<string, unknown> };
  } catch (err) {
    return { ok: false, error: err instanceof Error ? err.message : String(err) };
  }
}

/**
 * Dotted paths inside a completed node's result dict — the honest menu
 * for threaded references: `{node:<id>}.result.<one of these>` is what
 * the workflow executor actually resolves. Depth- and size-bounded, so a
 * deep result stays a readable row of chips.
 */
export function collectResultPaths(value: unknown, prefix = ""): string[] {
  if (!value || typeof value !== "object" || Array.isArray(value)) {
    return prefix ? [prefix] : [];
  }
  const out: string[] = [];
  // At each level, sibling scalars come before nested subtrees: the shallow
  // observed numbers (e.g. triangle_count) are the likeliest thread targets
  // and must not be truncated away by the chip cap behind spec's deep tree.
  const walk = (obj: Record<string, unknown>, path: string) => {
    if (path) out.push(path);
    if (path.split(".").length >= 3) return;
    const nested: string[] = [];
    for (const [key, val] of Object.entries(obj)) {
      if (val && typeof val === "object" && !Array.isArray(val)) {
        nested.push(key);
      } else {
        out.push(path ? `${path}.${key}` : key);
      }
    }
    for (const key of nested) {
      walk(obj[key] as Record<string, unknown>, path ? `${path}.${key}` : key);
    }
  };
  walk(value as Record<string, unknown>, prefix);
  return out.slice(0, 32);
}

/** Artifact entries the backend always ships; anything else is skipped. */
export function toArtifactList(artifacts: unknown[]): {
  artifact_id: string;
  type: string;
  path: string;
}[] {
  return artifacts.filter(
    (a): a is { artifact_id: string; type: string; path: string } => {
      const rec = a as Record<string, unknown> | null;
      return (
        !!rec &&
        typeof rec.artifact_id === "string" &&
        typeof rec.type === "string" &&
        typeof rec.path === "string"
      );
    },
  );
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

  // ---------------- M15.3: node editing, artifact links, remove guard ---
  // The engine/operation roster is the live registry's truth — the same
  // rule agents are bound to. `undefined` = still loading, `null` = the
  // listing is unavailable (the editor then degrades to typed inputs and
  // says so, instead of pretending no engines exist).
  const [engines, setEngines] = useState<EngineInfo[] | null | undefined>(
    undefined,
  );
  const [editing, setEditing] = useState<string | null>(null);
  const [draft, setDraft] = useState<EditorDraft | null>(null);
  const [removeNote, setRemoveNote] = useState<string | null>(null);
  const paramsRef = useRef<HTMLTextAreaElement>(null);

  useEffect(() => {
    if (!onNodesChange) return;
    let alive = true;
    void getEngines().then((list) => {
      if (alive) setEngines(list);
    });
    return () => {
      alive = false;
    };
  }, [onNodesChange]);

  const openEdit = useCallback((node: WorkflowNodeSpec) => {
    setRemoveNote(null);
    setEditing(node.id);
    setDraft({
      engine: node.engine,
      operation: node.operation,
      paramsText: JSON.stringify(node.parameters ?? {}, null, 2),
      deps: [...(node.dependencies ?? [])],
    });
  }, []);

  // Derived on every render — the save button cannot lie about validity.
  const paramsParse = draft ? tryParseParams(draft.paramsText) : null;

  const saveEdit = useCallback(() => {
    if (!draft || !editing || !onNodesChange) return;
    if (!paramsParse || !paramsParse.ok) return;
    const engine = draft.engine.trim();
    const operation = draft.operation.trim();
    if (!engine || !operation) return;
    onNodesChange(
      nodes.map((n) =>
        n.id === editing
          ? {
              ...n,
              engine,
              operation,
              parameters: paramsParse.parameters,
              dependencies: [...draft.deps],
            }
          : n,
      ),
    );
    setEditing(null);
    setDraft(null);
  }, [draft, editing, nodes, onNodesChange, paramsParse]);

  const toggleDep = useCallback((id: string) => {
    setDraft(
      (prev) =>
        prev && {
          ...prev,
          deps: prev.deps.includes(id)
            ? prev.deps.filter((dep) => dep !== id)
            : [...prev.deps, id],
        },
    );
  }, []);

  /**
   * Insert a `{node:<id>}.result[.<path>]` reference at the caret — or
   * over the current selection — where the operator is typing, never
   * appended out of sight at the end.
   */
  const insertRef = useCallback((ref: string) => {
    const ta = paramsRef.current;
    if (!ta) return;
    const start = ta.selectionStart ?? ta.value.length;
    const end = ta.selectionEnd ?? start;
    ta.setRangeText(ref, start, end, "end");
    setDraft((prev) => prev && { ...prev, paramsText: ta.value });
  }, []);

  const removeNode = useCallback(
    (id: string) => {
      if (!onNodesChange) return;
      const dependents = nodes
        .filter((n) => n.id !== id && (n.dependencies ?? []).includes(id))
        .map((n) => n.id);
      if (dependents.length > 0) {
        setRemoveNote(
          `cannot remove '${id}' — still required by: ${dependents.join(", ")}`,
        );
        return;
      }
      setRemoveNote(null);
      if (editing === id) {
        setEditing(null);
        setDraft(null);
      }
      onNodesChange(nodes.filter((n) => n.id !== id));
    },
    [editing, nodes, onNodesChange],
  );

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
                  {onNodesChange && editing !== node.id && (
                    <>
                      <button
                        type="button"
                        className="dag-edit-btn rounded border border-zinc-700 px-2 py-0.5 text-[10px] text-zinc-300 hover:border-zinc-500 disabled:opacity-40"
                        disabled={busy}
                        onClick={() => openEdit(node)}
                      >
                        EDIT
                      </button>
                      <button
                        type="button"
                        className="dag-remove-btn rounded border border-zinc-700 px-2 py-0.5 text-[10px] text-zinc-300 hover:border-rose-500 hover:text-rose-300 disabled:opacity-40"
                        disabled={busy}
                        onClick={() => removeNode(node.id)}
                        aria-label={`Remove node ${node.id}`}
                      >
                        ✕
                      </button>
                    </>
                  )}
                </div>
              </div>
              {outcome?.errors && outcome.errors.length > 0 && (
                <p className="pl-3 text-[11px] text-rose-400">
                  {outcome.errors[0].message}
                </p>
              )}
              {outcome && toArtifactList(outcome.artifacts).length > 0 && (
                <div className="flex flex-wrap gap-1.5 pl-3">
                  {toArtifactList(outcome.artifacts).map((a) => (
                    <a
                      key={a.artifact_id}
                      href={getArtifactUrl(a.artifact_id)}
                      title={a.path}
                      className="dag-artifact rounded border border-sky-500/60 bg-sky-500/10 px-2 py-0.5 text-[10px] text-sky-300 hover:bg-sky-500/20"
                    >
                      {a.type}
                    </a>
                  ))}
                </div>
              )}
              {editing === node.id && draft && (
                <NodeEditor
                  draft={draft}
                  onDraft={setDraft}
                  engines={engines}
                  others={nodes
                    .filter((n) => n.id !== node.id)
                    .map((n) => ({ id: n.id, outcome: outcomeById.get(n.id) }))}
                  paramsParse={paramsParse}
                  paramsRef={paramsRef}
                  onToggleDep={toggleDep}
                  onInsertRef={insertRef}
                  onSave={saveEdit}
                  onCancel={() => {
                    setEditing(null);
                    setDraft(null);
                  }}
                />
              )}
              {index < nodes.length - 1 && <span aria-hidden className="pl-4 text-zinc-700">↓</span>}
            </li>
          );
        })}
      </ol>

      {removeNote && (
        <p className="dag-remove-error mt-2 text-[11px] text-rose-400">{removeNote}</p>
      )}
    </section>
  );
}

/**
 * The inline node editor (M15.3): engine and operation from the live
 * registry roster — or typed inputs when the roster is unavailable,
 * stated as such — JSON parameters whose parse failure is shown
 * verbatim, dependency toggles over the other nodes, and the
 * threaded-ref helper. The save button stays disabled until the payload
 * is actually valid; the graph is never silently corrupted.
 */
function NodeEditor({
  draft,
  onDraft,
  engines,
  others,
  paramsParse,
  paramsRef,
  onToggleDep,
  onInsertRef,
  onSave,
  onCancel,
}: {
  draft: EditorDraft;
  onDraft: (next: EditorDraft) => void;
  engines: EngineInfo[] | null | undefined;
  others: { id: string; outcome?: WorkflowNodeOutcome }[];
  paramsParse: ReturnType<typeof tryParseParams> | null;
  paramsRef: RefObject<HTMLTextAreaElement | null>;
  onToggleDep: (id: string) => void;
  onInsertRef: (ref: string) => void;
  onSave: () => void;
  onCancel: () => void;
}) {
  const engineRoster = Array.isArray(engines) ? engines : null;
  const opsForEngine =
    engineRoster?.find((e) => e.name === draft.engine)?.capabilities ?? [];

  return (
    <div className="dag-editor mt-1 rounded-lg border border-zinc-700 bg-zinc-900/70 p-3 text-xs">
      <div className="flex gap-3">
        <label className="flex flex-col gap-1">
          <span className="text-[10px] font-semibold tracking-wider text-zinc-400">
            ENGINE
          </span>
          {engineRoster ? (
            <select
              value={draft.engine}
              onChange={(e) => onDraft({ ...draft, engine: e.target.value })}
              className="dag-engine-select min-w-[140px] rounded border border-zinc-700 bg-zinc-900 px-2 py-1 text-zinc-200"
            >
              {!engineRoster.some((e) => e.name === draft.engine) && draft.engine && (
                <option value={draft.engine}>{draft.engine}</option>
              )}
              {engineRoster.map((e) => (
                <option key={e.name} value={e.name}>
                  {e.name}
                </option>
              ))}
            </select>
          ) : (
            <>
              <input
                value={draft.engine}
                onChange={(e) => onDraft({ ...draft, engine: e.target.value })}
                className="dag-engine-input rounded border border-zinc-700 bg-zinc-900 px-2 py-1 text-zinc-200"
              />
              {!engineRoster && (
                <span className="text-[10px] text-zinc-500">
                  {engines === undefined
                    ? "loading engine roster…"
                    : "engine roster unavailable — type the engine name"}
                </span>
              )}
            </>
          )}
        </label>
        <label className="flex flex-col gap-1">
          <span className="text-[10px] font-semibold tracking-wider text-zinc-400">
            OPERATION
          </span>
          {engineRoster ? (
            <select
              value={draft.operation}
              onChange={(e) => onDraft({ ...draft, operation: e.target.value })}
              className="dag-operation-select min-w-[140px] rounded border border-zinc-700 bg-zinc-900 px-2 py-1 text-zinc-200"
            >
              {!opsForEngine.includes(draft.operation) && draft.operation && (
                <option value={draft.operation}>{draft.operation}</option>
              )}
              {opsForEngine.map((op) => (
                <option key={op} value={op}>
                  {op}
                </option>
              ))}
            </select>
          ) : (
            <input
              value={draft.operation}
              onChange={(e) => onDraft({ ...draft, operation: e.target.value })}
              className="dag-operation-input rounded border border-zinc-700 bg-zinc-900 px-2 py-1 text-zinc-200"
            />
          )}
        </label>
        <div className="flex flex-col gap-1">
          <span className="text-[10px] font-semibold tracking-wider text-zinc-400">
            DEPENDENCIES
          </span>
          <div className="flex flex-wrap gap-1.5">
            {others.length === 0 ? (
              <span className="text-[10px] text-zinc-500">no other nodes</span>
            ) : (
              others.map((o) => {
                const on = draft.deps.includes(o.id);
                return (
                  <button
                    type="button"
                    key={o.id}
                    className={`dag-dep-chip rounded border px-2 py-0.5 font-mono text-[10px] ${
                      on
                        ? "border-sky-500 bg-sky-500/20 text-sky-200"
                        : "border-zinc-700 bg-zinc-900 text-zinc-400 hover:border-zinc-500"
                    }`}
                    onClick={() => onToggleDep(o.id)}
                  >
                    {on ? "☑" : "☐"} {o.id}
                  </button>
                );
              })
            )}
          </div>
        </div>
      </div>

      <label className="mt-3 flex flex-col gap-1">
        <span className="text-[10px] font-semibold tracking-wider text-zinc-400">
          PARAMETERS (JSON)
        </span>
        <textarea
          ref={paramsRef}
          value={draft.paramsText}
          onChange={(e) => onDraft({ ...draft, paramsText: e.target.value })}
          rows={4}
          spellCheck={false}
          className="dag-params-input rounded border border-zinc-700 bg-zinc-900 px-2 py-1 font-mono text-[11px] text-zinc-200 focus:border-sky-600 focus:outline-none"
        />
        {paramsParse && !paramsParse.ok && (
          <span className="dag-params-error text-[10px] text-rose-400">
            {paramsParse.error}
          </span>
        )}
      </label>

      <div className="dag-thread-section mt-2">
        <span className="text-[10px] font-semibold tracking-wider text-zinc-400">
          THREAD VALUES FORWARD — inserted at the caret
        </span>
        {others.length === 0 ? (
          <p className="dag-thread-none mt-1 text-[10px] text-zinc-500">
            no other nodes to thread from
          </p>
        ) : (
          <div className="mt-1 flex flex-wrap items-center gap-1.5">
            {others.map((o) => {
              const paths =
                o.outcome?.status === "completed"
                  ? collectResultPaths(o.outcome.result).slice(0, 4)
                  : [];
              return (
                <div key={o.id} className="flex flex-wrap items-center gap-1.5">
                  <button
                    type="button"
                    className="dag-thread-btn rounded border border-zinc-700 px-2 py-0.5 font-mono text-[10px] text-zinc-300 hover:border-zinc-500"
                    onClick={() => onInsertRef(`{node:${o.id}}.result.`)}
                    title={`insert {node:${o.id}}.result. at the caret`}
                  >
                    {`{node:${o.id}}.result.`}
                  </button>
                  {paths.map((p) => (
                    <button
                      type="button"
                      key={p}
                      className="dag-thread-path rounded border border-sky-500/50 bg-sky-500/10 px-2 py-0.5 font-mono text-[10px] text-sky-300 hover:bg-sky-500/20"
                      title={`observed last run: ${o.id}.result.${p}`}
                      onClick={() => onInsertRef(`{node:${o.id}}.result.${p}`)}
                    >
                      {o.id}.result.{p}
                    </button>
                  ))}
                </div>
              );
            })}
          </div>
        )}
      </div>

      <div className="mt-3 flex justify-end gap-2">
        <button
          type="button"
          className="dag-cancel-btn rounded border border-zinc-700 px-3 py-1 text-zinc-300 hover:border-zinc-500"
          onClick={onCancel}
        >
          CANCEL
        </button>
        <button
          type="button"
          className="dag-save-btn rounded border border-emerald-500/60 bg-emerald-500/10 px-3 py-1 font-semibold text-emerald-300 hover:bg-emerald-500/20 disabled:opacity-40"
          disabled={
            !paramsParse?.ok || !draft.engine.trim() || !draft.operation.trim()
          }
          onClick={onSave}
        >
          SAVE NODE
        </button>
      </div>
    </div>
  );
}

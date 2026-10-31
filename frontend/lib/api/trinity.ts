/**
 * Trinity API — isolated data layer
 * Preserves POST /requirements/execute contract and response envelope.
 */

export const TRINITY_API_URL =
  process.env.NEXT_PUBLIC_TRINITY_API_URL ?? 'http://localhost:8000/api';

export type ArtifactOut = {
  artifact_id: string;
  type: string;
  path: string;
  size_bytes: number;
  checksum: string;
};

export type ValidationOut = {
  status: 'GENERATED' | 'VALIDATED' | 'VERIFIED' | 'FAILED';
  checks: Record<string, unknown>;
};

export type CadParameter = {
  name: string;
  label?: string;
  description?: string;
  unit: string;
  default: number;
  min?: number | null;
  min_exclusive?: boolean;
  max?: number | null;
  integer?: boolean;
  allowed_values?: number[] | null;
};

export type CadPart = {
  name: string;
  description?: string;
  parameters: CadParameter[];
  examples?: string[];
  example_sentences?: string[];
};

export type ParseSummary = {
  matched_rule?: string | { name?: string; [key: string]: unknown };
  rule?: string | { name?: string; [key: string]: unknown };
  extracted_values?: Record<string, unknown>;
  values?: Record<string, unknown>;
  defaults?: Array<string | { name?: string; value?: unknown; [key: string]: unknown }> | Record<string, unknown>;
  defaults_filled?: Array<string | { name?: string; parameter?: string; key?: string; value?: unknown; [key: string]: unknown }> | Record<string, unknown>;
  [key: string]: unknown;
};

export type TrinityResult = {
  success: boolean;
  engine: string;
  operation: string;
  result: {
    spec?: { type?: string; units?: string; parameters: Record<string, unknown> };
    triangle_count?: number;
    parse?: ParseSummary;
    [key: string]: unknown;
  };
  artifacts: ArtifactOut[];
  validation: ValidationOut | null;
  errors: { message: string; code?: string; details?: unknown }[];
  job_id: string;
};

export class TrinityApiError extends Error {
  readonly status: number;
  readonly suggestions: string[];
  readonly examples: string[];

  constructor(message: string, status: number, suggestions: string[] = [], examples: string[] = []) {
    super(message);
    this.name = 'TrinityApiError';
    this.status = status;
    this.suggestions = suggestions;
    this.examples = examples;
  }
}

function toTextList(value: unknown): string[] {
  if (!Array.isArray(value)) return [];
  return value
    .map((item) => {
      if (typeof item === 'string') return item;
      if (item && typeof item === 'object') {
        const entry = item as Record<string, unknown>;
        return typeof entry.name === 'string' ? entry.name : typeof entry.text === 'string' ? entry.text : '';
      }
      return '';
    })
    .filter(Boolean);
}

function apiError(payload: unknown, status: number): TrinityApiError {
  const body = payload && typeof payload === 'object' ? payload as Record<string, unknown> : {};
  const errors = Array.isArray(body.errors) ? body.errors : [];
  const first = errors[0] && typeof errors[0] === 'object' ? errors[0] as Record<string, unknown> : {};
  const details = first.details && typeof first.details === 'object' ? first.details as Record<string, unknown> : {};
  const message = typeof first.message === 'string'
    ? first.message
    : typeof body.detail === 'string'
      ? body.detail
      : `Request failed (${status})`;
  const suggestions = [
    ...toTextList(details.suggestions),
    ...toTextList(details.nearest_parts),
    ...toTextList(details.matching_parts),
    ...toTextList(details.supported),
    ...toTextList(details.supported_types),
  ];
  const examples = [
    ...toTextList(details.examples),
    ...toTextList(details.example_sentences),
  ];
  return new TrinityApiError(message, status, [...new Set(suggestions)], [...new Set(examples)]);
}

/** What the backend can actually write for each CAD output format. */
export type CadOutputSpec = {
  available: boolean;
  reason?: string;
  representation?: string;
  schema?: string;
  unit?: string;
  kernel?: string;
};

const OUTPUT_FORMATS = ['stl', 'glb', 'json', 'step'] as const;

export async function getCadOutputs(): Promise<Record<string, CadOutputSpec>> {
  const response = await fetch(`${TRINITY_API_URL}/cad/catalog`, { cache: 'no-store' });
  const payload: unknown = await response.json().catch(() => null);
  if (!response.ok) throw apiError(payload, response.status);

  const body = payload && typeof payload === 'object' ? payload as Record<string, unknown> : {};
  const declared = body.outputs && typeof body.outputs === 'object' ? body.outputs as Record<string, unknown> : {};
  const specs: Record<string, CadOutputSpec> = {};
  for (const format of OUTPUT_FORMATS) {
    const spec = declared[format];
    specs[format] = spec && typeof spec === 'object' && 'available' in spec
      ? spec as CadOutputSpec
      : { available: false, reason: 'The backend did not report this format.' };
  }
  return specs;
}

export async function getCadCatalog(): Promise<CadPart[]> {
  const response = await fetch(`${TRINITY_API_URL}/cad/catalog`, { cache: 'no-store' });
  const payload: unknown = await response.json().catch(() => null);
  if (!response.ok) throw apiError(payload, response.status);

  const body = payload && typeof payload === 'object' ? payload as Record<string, unknown> : {};
  const rows = Array.isArray(payload) ? payload : Array.isArray(body.parts) ? body.parts : [];
  return rows.filter((row): row is CadPart => {
    return !!row && typeof row === 'object' && typeof (row as CadPart).name === 'string' && Array.isArray((row as CadPart).parameters);
  });
}

export async function generateCad(input: {
  type: string;
  parameters: Record<string, number>;
  outputs: string[];
}): Promise<TrinityResult> {
  const response = await fetch(`${TRINITY_API_URL}/cad/generate`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(input),
  });
  const payload: unknown = await response.json().catch(() => null);
  if (!response.ok) throw apiError(payload, response.status);
  if (!payload || typeof payload !== 'object' || !('success' in payload) || !('job_id' in payload)) {
    throw new Error('The API returned an invalid response.');
  }
  return payload as TrinityResult;
}

export type EngineInfo = {
  name: string;
  version: string;
  capabilities: string[];
  status?: string | null;
};

export async function getEngines(): Promise<EngineInfo[] | null> {
  try {
    const response = await fetch(`${TRINITY_API_URL}/engines`, { cache: 'no-store' });
    if (!response.ok) return null;
    const payload: unknown = await response.json().catch(() => null);
    if (!Array.isArray(payload)) return null;
    const rows = payload.filter(
      (row): row is EngineInfo =>
        !!row && typeof row === 'object' && typeof (row as EngineInfo).name === 'string'
    );
    return rows;
  } catch {
    return null;
  }
}

export async function executeEngine(
  engine: string,
  operation: string,
  parameters: Record<string, unknown> = {},
): Promise<TrinityResult> {
  const response = await fetch(`${TRINITY_API_URL}/execute`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ engine, operation, parameters }),
  });

  const payload: unknown = await response.json().catch(() => null);

  if (!response.ok) {
    throw apiError(payload, response.status);
  }

  if (
    !payload ||
    typeof payload !== 'object' ||
    !('success' in payload) ||
    !('job_id' in payload)
  ) {
    throw new Error('The API returned an invalid response.');
  }

  return payload as TrinityResult;
}

export async function uploadImage(file: File): Promise<ArtifactOut> {
  const body = new FormData();
  body.append('file', file);
  const response = await fetch(`${TRINITY_API_URL}/uploads`, { method: 'POST', body });
  const payload: unknown = await response.json().catch(() => null);
  if (!response.ok) throw apiError(payload, response.status);
  if (
    !payload ||
    typeof payload !== 'object' ||
    !('artifact_id' in payload) ||
    !('checksum' in payload)
  ) {
    throw new Error('The upload returned an invalid response.');
  }
  return payload as ArtifactOut;
}

export function getArtifactUrl(artifactId: string): string {
  return `${TRINITY_API_URL}/artifacts/${artifactId}`;
}

// ----------------------------- M15: streamed plan over SSE -----------------

export type PlanNodeSpec = {
  id: string;
  engine: string;
  operation: string;
  parameters: Record<string, unknown>;
  dependencies: string[];
};

export type StreamPlanResult =
  | { type: 'final'; source: string; summary: string; nodes: PlanNodeSpec[] }
  | { type: 'error'; code: string; message: string };

function specIsNode(value: unknown): value is PlanNodeSpec {
  if (!value || typeof value !== 'object') return false;
  const spec = value as Partial<PlanNodeSpec>;
  return (
    typeof spec.id === 'string' && spec.id !== '' &&
    typeof spec.engine === 'string' && spec.engine !== '' &&
    typeof spec.operation === 'string' && spec.operation !== '' &&
    typeof spec.parameters === 'object' && spec.parameters !== null &&
    Array.isArray(spec.dependencies)
  );
}

/**
 * Stream POST /api/intelligence/stream-plan.
 *
 * Delta tokens surface through `onDelta` as they arrive; the resolved
 * outcome is returned as the last SSE frame: a `final` carrying the
 * validated executable plan (nodes in POST /api/workflows/execute's exact
 * shape) or an honest `error` frame (code + verbatim message) for an
 * unparseable model reply. A non-OK HTTP response throws TrinityApiError
 * like every other call.
 */
export async function streamPlan(
  text: string,
  onDelta: (text: string) => void,
): Promise<StreamPlanResult> {
  const response = await fetch(`${TRINITY_API_URL}/intelligence/stream-plan`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ text }),
  });
  if (!response.ok) {
    // Only for error responses: reading the body locks the stream, and a
    // 200 SSE body must stay unlocked for the reader below.
    const payload: unknown = await response.json().catch(() => null);
    throw apiError(payload, response.status);
  }
  if (!response.body) throw new Error('The API returned an empty stream.');

  const reader = response.body.getReader();
  const decoder = new TextDecoder();
  let buffer = '';
  let last: StreamPlanResult | null = null;

  const handle = (frame: string) => {
    const line = frame.trim();
    if (!line.startsWith('data:')) return;
    let event: Record<string, unknown>;
    try {
      event = JSON.parse(line.slice(5).trim()) as Record<string, unknown>;
    } catch {
      return; // malformed frame: skip, never invent text
    }
    if (event.type === 'delta' && typeof event.text === 'string') {
      onDelta(event.text);
    } else if (event.type === 'error') {
      const message =
        event.message && typeof event.message === 'object'
          ? (event.message as Record<string, unknown>)
          : {};
      last = {
        type: 'error',
        code: typeof message.code === 'string' ? message.code : 'stream_error',
        message:
          typeof message.message === 'string' ? message.message : 'planning failed',
      };
    } else if (event.type === 'final') {
      const plan =
        event.plan && typeof event.plan === 'object'
          ? (event.plan as Record<string, unknown>)
          : {};
      last = {
        type: 'final',
        source: typeof plan.source === 'string' ? plan.source : '',
        summary: typeof plan.summary === 'string' ? plan.summary : '',
        nodes: Array.isArray(plan.nodes)
          ? (plan.nodes as unknown[]).filter(specIsNode)
          : [],
      };
    }
  };

  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    buffer += decoder.decode(value, { stream: true });
    const frames = buffer.split('\n\n');
    buffer = frames.pop() ?? '';
    for (const frame of frames) handle(frame);
  }
  if (buffer.trim()) handle(buffer); // trailing frame without its blank line
  if (!last) throw new Error('The stream ended without a final frame.');
  return last;
}

export async function executeRequirement(text: string): Promise<TrinityResult> {
  const response = await fetch(`${TRINITY_API_URL}/requirements/execute`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ text }),
  });

  const payload: unknown = await response.json().catch(() => null);

  if (!response.ok) {
    throw apiError(payload, response.status);
  }

  if (
    !payload ||
    typeof payload !== 'object' ||
    !('success' in payload) ||
    !('job_id' in payload)
  ) {
    throw new Error('The API returned an invalid response.');
  }

  return payload as TrinityResult;
}

export type ExecutionStage =
  | 'idle'
  | 'analyzing'
  | 'building'
  | 'validating'
  | 'complete';

export const STAGE_LABELS: Record<ExecutionStage, string> = {
  idle: 'READY',
  analyzing: 'ANALYZING...',
  building: 'BUILDING GEOMETRY...',
  validating: 'VALIDATING...',
  complete: 'COMPLETE',
};

/** Parsed /api/update/check answer. available:false always carries a reason. */
export type UpdateCheck = {
  available: boolean;
  version?: string;
  published_at?: string;
  size_bytes?: number | null;
  reason?: string;
};

/** Result of POST /api/update/apply. staged implies restart_required. */
export type UpdateApply = {
  staged: boolean;
  version?: string;
  restart_required?: boolean;
};

function coerceUpdateCheck(payload: unknown): UpdateCheck {
  const body = payload && typeof payload === 'object' ? payload as Record<string, unknown> : {};
  const available = body.available === true;
  const out: UpdateCheck = { available };
  if (typeof body.version === 'string') out.version = body.version;
  if (typeof body.published_at === 'string') out.published_at = body.published_at;
  if (typeof body.size_bytes === 'number') out.size_bytes = body.size_bytes;
  if (typeof body.reason === 'string') out.reason = body.reason;
  return out;
}

/** Honest availability answer; never downloads, never applies. */
export async function checkUpdate(): Promise<UpdateCheck> {
  const response = await fetch(`${TRINITY_API_URL}/update/check`, { cache: 'no-store' });
  const payload: unknown = await response.json().catch(() => null);
  if (!response.ok) {
    return { available: false, reason: `Update check unavailable (${response.status})` };
  }
  return coerceUpdateCheck(payload);
}

/**
 * Explicit user action: stage a hash-verified update. Never call this
 * without a real user decision; the backend requires confirm anyway.
 */
export async function applyUpdate(confirm: boolean): Promise<UpdateApply> {
  const response = await fetch(`${TRINITY_API_URL}/update/apply`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ confirm }),
  });
  const payload: unknown = await response.json().catch(() => null);
  if (!response.ok) {
    throw apiError(payload, response.status);
  }
  const body = payload && typeof payload === 'object' ? payload as Record<string, unknown> : {};
  const out: UpdateApply = { staged: body.success === true };
  if (typeof body.version === 'string') out.version = body.version;
  if (body.restart_required !== undefined) out.restart_required = body.restart_required === true;
  if (out.staged && out.restart_required === undefined) out.restart_required = true;
  if (!out.staged) {
    const errors = Array.isArray(body.errors) ? body.errors : [];
    const first = errors[0] && typeof errors[0] === 'object' ? errors[0] as Record<string, unknown> : {};
    throw new Error(typeof first.message === 'string' ? first.message : 'Update could not be staged.');
  }
  return out;
}

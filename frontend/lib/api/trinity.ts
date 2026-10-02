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

export function getArtifactUrl(artifactId: string): string {
  return `${TRINITY_API_URL}/artifacts/${artifactId}`;
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

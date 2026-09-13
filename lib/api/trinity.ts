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

export type TrinityResult = {
  success: boolean;
  engine: string;
  operation: string;
  result: {
    spec?: { parameters: Record<string, number> };
    triangle_count?: number;
    [key: string]: unknown;
  };
  artifacts: ArtifactOut[];
  validation: ValidationOut | null;
  errors: { message: string; code?: string; details?: unknown }[];
  job_id: string;
};

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
    const apiError = payload as
      | { detail?: string; errors?: { message?: string }[] }
      | null;
    const message =
      apiError?.errors?.[0]?.message ??
      apiError?.detail ??
      `Request failed (${response.status})`;
    throw new Error(message);
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

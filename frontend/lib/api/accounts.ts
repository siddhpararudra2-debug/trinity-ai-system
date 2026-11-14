/**
 * M30 accounts — isolated data layer for signup, verify, login, refresh,
 * logout, reset and deletion. Same conventions as trinity.ts: plain
 * fetch against TRINITY_API_URL, the platform error envelope surfaced
 * through TrinityApiError, no invented fields.
 */

import { TRINITY_API_URL } from './trinity';

export type AccountMe = {
  user_id: string;
  email: string;
  verified: boolean;
  created_at?: string | null;
  dev?: boolean;
};

export type InlineToken = {
  delivery: 'inline' | 'none';
  token?: string;
  expires_at?: string;
  note?: string;
};

export type Session = {
  access_token: string;
  refresh_token: string | null;
  expires_in: number;
  dev?: boolean;
};

/** The platform error envelope: {success:false, errors:[{code,message,details}]}. */
export class AccountsError extends Error {
  readonly status: number;
  readonly code?: string;
  readonly details?: unknown;

  constructor(message: string, status: number, code?: string, details?: unknown) {
    super(message);
    this.name = 'AccountsError';
    this.status = status;
    this.code = code;
    this.details = details;
  }
}

async function request<T>(path: string, init: RequestInit): Promise<T> {
  let response: Response;
  try {
    response = await fetch(`${TRINITY_API_URL}${path}`, {
      ...init,
      headers: { 'Content-Type': 'application/json', ...init.headers },
    });
  } catch {
    throw new AccountsError(
      'The API is unreachable. Start the backend (start.ps1) and try again.',
      0,
    );
  }
  if (!response.ok) {
    let message = `Request failed (${response.status})`;
    let code: string | undefined;
    let details: unknown;
    try {
      const body = (await response.json()) as {
        errors?: Array<{ message?: string; code?: string; details?: unknown }>;
      };
      const first = body.errors?.[0];
      if (first?.message) message = first.message;
      code = first?.code;
      details = first?.details;
    } catch {
      /* non-JSON error body: keep the status-line message */
    }
    throw new AccountsError(message, response.status, code, details);
  }
  return (await response.json()) as T;
}

export function signup(email: string, password: string): Promise<{
  user_id: string;
  email: string;
  verified: boolean;
  verification: InlineToken;
}> {
  return request('/accounts/signup', {
    method: 'POST',
    body: JSON.stringify({ email, password }),
  });
}

export function verify(token: string): Promise<{ email: string; verified: boolean }> {
  return request('/accounts/verify', {
    method: 'POST',
    body: JSON.stringify({ token }),
  });
}

export function login(username: string, password: string): Promise<Session> {
  return request('/login', {
    method: 'POST',
    body: JSON.stringify({ username, password }),
  });
}

export function refresh(refresh_token: string): Promise<Session> {
  return request('/accounts/refresh', {
    method: 'POST',
    body: JSON.stringify({ refresh_token }),
  });
}

export function logout(refresh_token: string): Promise<{ logged_out: boolean }> {
  return request('/accounts/logout', {
    method: 'POST',
    body: JSON.stringify({ refresh_token }),
  });
}

export function resetRequest(email: string): Promise<InlineToken> {
  return request('/accounts/reset-request', {
    method: 'POST',
    body: JSON.stringify({ email }),
  });
}

export function resetConfirm(
  token: string,
  new_password: string,
): Promise<{ email: string; password_reset: boolean }> {
  return request('/accounts/reset-confirm', {
    method: 'POST',
    body: JSON.stringify({ token, new_password }),
  });
}

export function me(accessToken: string): Promise<AccountMe> {
  return request('/accounts/me', {
    method: 'GET',
    headers: { Authorization: `Bearer ${accessToken}` },
  });
}

export function deleteMe(accessToken: string): Promise<{ deleted: boolean }> {
  return request('/accounts/me', {
    method: 'DELETE',
    headers: { Authorization: `Bearer ${accessToken}` },
  });
}

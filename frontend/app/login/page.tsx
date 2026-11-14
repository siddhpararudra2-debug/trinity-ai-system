'use client';

/**
 * M30: the in-app login screen. Sign in, create an account, verify the
 * address, refresh, log out and delete the account — everything the M30
 * accounts API actually offers, nothing invented. Signed-out visitors
 * keep using the free tier everywhere else; this screen only manages
 * the account itself.
 */

import { useEffect, useState } from 'react';
import Link from 'next/link';
import {
  AccountsError,
  deleteMe,
  login,
  logout,
  me,
  refresh as refreshSession,
  resetConfirm,
  resetRequest,
  signup,
  verify,
  type AccountMe,
  type InlineToken,
} from '@/lib/api/accounts';

type Mode = 'signin' | 'create' | 'reset';

const ACCESS_KEY = 'trinity.accounts.access';
const REFRESH_KEY = 'trinity.accounts.refresh';

function readStored(key: string): string | null {
  try {
    return window.sessionStorage.getItem(key);
  } catch {
    return null;
  }
}

function storeSession(access: string, refresh: string | null): void {
  try {
    window.sessionStorage.setItem(ACCESS_KEY, access);
    if (refresh) window.sessionStorage.setItem(REFRESH_KEY, refresh);
    else window.sessionStorage.removeItem(REFRESH_KEY);
  } catch {
    /* private mode: the session lives for this page view only */
  }
}

function clearSession(): void {
  try {
    window.sessionStorage.removeItem(ACCESS_KEY);
    window.sessionStorage.removeItem(REFRESH_KEY);
  } catch {
    /* nothing to clear */
  }
}

function describe(err: unknown): string {
  if (err instanceof AccountsError) {
    if (err.code === 'account_locked') {
      const details = err.details as { retry_after?: number } | undefined;
      const wait = details?.retry_after;
      return wait
        ? `${err.message} — try again in about ${wait} s.`
        : err.message;
    }
    if (
      err.details &&
      typeof err.details === 'object' &&
      (err.details as { needs_verification?: boolean }).needs_verification
    ) {
      return `${err.message} — verify the address first (the token was shown when you signed up).`;
    }
    return err.message;
  }
  return 'Something went wrong. Try again.';
}

export default function LoginPage() {
  const [mode, setMode] = useState<Mode>('signin');
  const [email, setEmail] = useState('');
  const [password, setPassword] = useState('');
  const [newPassword, setNewPassword] = useState('');
  const [inlineToken, setInlineToken] = useState('');
  const [pending, setPending] = useState<InlineToken | null>(null);
  const [account, setAccount] = useState<AccountMe | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  const loadAccount = async (access: string): Promise<boolean> => {
    try {
      const info = await me(access);
      setAccount(info);
      return true;
    } catch {
      clearSession();
      return false;
    }
  };

  useEffect(() => {
    const access = readStored(ACCESS_KEY);
    if (!access) return;
    let cancelled = false;
    // Promise callbacks, not effect-body calls: the session check is an
    // external-system read, and state updates land in its continuation.
    me(access)
      .then((info) => {
        if (!cancelled) setAccount(info);
      })
      .catch(() => {
        if (!cancelled) clearSession();
      });
    return () => {
      cancelled = true;
    };
  }, []);

  const onSignIn = async (e: React.FormEvent) => {
    e.preventDefault();
    setBusy(true);
    setError(null);
    setNotice(null);
    try {
      const session = await login(email.trim(), password);
      storeSession(session.access_token, session.refresh_token);
      await loadAccount(session.access_token);
      setPassword('');
      if (session.dev) {
        setNotice('Signed in to the development instance (no stored account).');
      }
    } catch (err) {
      setError(describe(err));
    } finally {
      setBusy(false);
    }
  };

  const onCreate = async (e: React.FormEvent) => {
    e.preventDefault();
    setBusy(true);
    setError(null);
    setNotice(null);
    try {
      const created = await signup(email.trim(), password);
      setPending(created.verification);
      setNotice(
        created.verification.delivery === 'inline'
          ? 'Account created. No mailer is configured in this deployment — the API returned the verification token inline below.'
          : 'Account created.',
      );
      setPassword('');
    } catch (err) {
      setError(describe(err));
    } finally {
      setBusy(false);
    }
  };

  const onVerify = async () => {
    setBusy(true);
    setError(null);
    setNotice(null);
    try {
      const result = await verify(inlineToken.trim());
      setNotice(`Address verified for ${result.email}. You can sign in now.`);
      setPending(null);
      setInlineToken('');
      setMode('signin');
    } catch (err) {
      setError(describe(err));
    } finally {
      setBusy(false);
    }
  };

  const onRefreshSession = async () => {
    const refresh = readStored(REFRESH_KEY);
    if (!refresh) {
      setError('No refresh token in this tab. Sign in again.');
      return;
    }
    setBusy(true);
    setError(null);
    setNotice(null);
    try {
      const rotated = await refreshSession(refresh);
      storeSession(rotated.access_token, rotated.refresh_token);
      await loadAccount(rotated.access_token);
      setNotice('Session refreshed.');
    } catch (err) {
      setError(describe(err));
    } finally {
      setBusy(false);
    }
  };

  const onLogout = async () => {
    const refresh = readStored(REFRESH_KEY);
    setBusy(true);
    setError(null);
    setNotice(null);
    try {
      if (refresh) await logout(refresh);
    } catch {
      /* the server may already consider it dead; clear locally anyway */
    } finally {
      clearSession();
      setAccount(null);
      setBusy(false);
    }
  };

  const onDelete = async () => {
    const access = readStored(ACCESS_KEY);
    if (!access || !account) return;
    setBusy(true);
    setError(null);
    setNotice(null);
    try {
      await deleteMe(access);
      clearSession();
      setAccount(null);
      setNotice(`Account ${account.email} deleted.`);
    } catch (err) {
      setError(describe(err));
    } finally {
      setBusy(false);
    }
  };

  const onResetRequest = async (e: React.FormEvent) => {
    e.preventDefault();
    setBusy(true);
    setError(null);
    setNotice(null);
    try {
      const result = await resetRequest(email.trim());
      if (result.delivery === 'inline' && result.token) {
        setPending(result);
        setNotice(
          'No mailer is configured in this deployment — the reset token is shown below. Set a new password next.',
        );
      } else {
        setNotice(
          'If that address has an account, a reset link was issued. No token was returned for this address.',
        );
      }
      setPassword('');
    } catch (err) {
      setError(describe(err));
    } finally {
      setBusy(false);
    }
  };

  const onResetConfirm = async (e: React.FormEvent) => {
    e.preventDefault();
    setBusy(true);
    setError(null);
    setNotice(null);
    try {
      const result = await resetConfirm(inlineToken.trim(), newPassword);
      setNotice(
        `Password reset for ${result.email}. Sign in with the new password.`,
      );
      setPending(null);
      setInlineToken('');
      setNewPassword('');
      setMode('signin');
    } catch (err) {
      setError(describe(err));
    } finally {
      setBusy(false);
    }
  };

  return (
    <main>
      <section className="page-hero">
        <div className="container">
          <p className="eyebrow">Account</p>
          <h1>
            Sign in, <span className="accent-word">or don&apos;t.</span>
          </h1>
          <p className="lede">
            Signed-out use stays on the free tier — every workspace feature
            works without an account. This screen manages the account itself:
            signup, verification, sessions and deletion.
          </p>
        </div>
      </section>

      <section className="section-sm" aria-label="Account forms">
        <div className="container">
          <div style={{ maxWidth: 560, margin: '0 auto' }}>
            {error && (
              <p role="alert" className="ws-empty" style={{ color: '#ff8f8f' }}>
                {error}
              </p>
            )}
            {notice && (
              <p role="status" className="ws-empty" style={{ color: '#8fd4a8' }}>
                {notice}
              </p>
            )}

            {account ? (
              <article className="ws-panel">
                <h2>Signed in</h2>
                <p className="ws-empty">
                  {account.email}
                  {account.dev ? ' · development identity (not a stored account)' : ''}
                  {account.verified ? ' · verified' : ' · not verified'}
                </p>
                {account.created_at && (
                  <p className="ws-empty">Account since {account.created_at}</p>
                )}
                <div style={{ display: 'flex', gap: 12, marginTop: 16, flexWrap: 'wrap' }}>
                  <button type="button" className="btn btn-ghost" onClick={onRefreshSession} disabled={busy}>
                    Refresh session
                  </button>
                  <button type="button" className="btn btn-ghost" onClick={onLogout} disabled={busy}>
                    Log out
                  </button>
                  <button
                    type="button"
                    className="btn btn-ghost"
                    onClick={onDelete}
                    disabled={busy || Boolean(account.dev)}
                    title={account.dev ? 'A development identity has no stored account to delete' : ''}
                  >
                    Delete my account
                  </button>
                </div>
              </article>
            ) : (
              <article className="ws-panel">
                <div style={{ display: 'flex', gap: 12, marginBottom: 20 }}>
                  {(['signin', 'create', 'reset'] as Mode[]).map((m) => (
                    <button
                      key={m}
                      type="button"
                      className={`btn ${mode === m ? 'btn-primary' : 'btn-ghost'}`}
                      onClick={() => {
                        setMode(m);
                        setError(null);
                        setNotice(null);
                        setPending(null);
                      }}
                    >
                      {m === 'signin' ? 'Sign in' : m === 'create' ? 'Create account' : 'Reset password'}
                    </button>
                  ))}
                </div>

                {mode === 'signin' && (
                  <form onSubmit={onSignIn}>
                    <label className="ws-empty" htmlFor="login-email">
                      Email
                    </label>
                    <input
                      id="login-email"
                      type="email"
                      required
                      value={email}
                      onChange={(e) => setEmail(e.target.value)}
                      style={{ width: '100%', marginBottom: 12 }}
                      autoComplete="email"
                    />
                    <label className="ws-empty" htmlFor="login-password">
                      Password
                    </label>
                    <input
                      id="login-password"
                      type="password"
                      required
                      value={password}
                      onChange={(e) => setPassword(e.target.value)}
                      style={{ width: '100%', marginBottom: 16 }}
                      autoComplete="current-password"
                    />
                    <button type="submit" className="btn btn-primary" disabled={busy}>
                      {busy ? 'Working…' : 'Sign in'}
                    </button>
                  </form>
                )}

                {mode === 'create' && (
                  <form onSubmit={onCreate}>
                    <label className="ws-empty" htmlFor="signup-email">
                      Email
                    </label>
                    <input
                      id="signup-email"
                      type="email"
                      required
                      value={email}
                      onChange={(e) => setEmail(e.target.value)}
                      style={{ width: '100%', marginBottom: 12 }}
                      autoComplete="email"
                    />
                    <label className="ws-empty" htmlFor="signup-password">
                      Password (8+ characters)
                    </label>
                    <input
                      id="signup-password"
                      type="password"
                      required
                      minLength={8}
                      value={password}
                      onChange={(e) => setPassword(e.target.value)}
                      style={{ width: '100%', marginBottom: 16 }}
                      autoComplete="new-password"
                    />
                    <button type="submit" className="btn btn-primary" disabled={busy}>
                      {busy ? 'Working…' : 'Create account'}
                    </button>
                  </form>
                )}

                {mode === 'reset' && !pending && (
                  <form onSubmit={onResetRequest}>
                    <label className="ws-empty" htmlFor="reset-email">
                      Email
                    </label>
                    <input
                      id="reset-email"
                      type="email"
                      required
                      value={email}
                      onChange={(e) => setEmail(e.target.value)}
                      style={{ width: '100%', marginBottom: 16 }}
                      autoComplete="email"
                    />
                    <button type="submit" className="btn btn-primary" disabled={busy}>
                      {busy ? 'Working…' : 'Request reset token'}
                    </button>
                  </form>
                )}

                {mode === 'reset' && pending && (
                  <form onSubmit={onResetConfirm}>
                    <label className="ws-empty" htmlFor="reset-token">
                      Reset token
                    </label>
                    <input
                      id="reset-token"
                      type="text"
                      required
                      value={inlineToken}
                      onChange={(e) => setInlineToken(e.target.value)}
                      style={{ width: '100%', marginBottom: 12 }}
                    />
                    <label className="ws-empty" htmlFor="reset-password">
                      New password (8+ characters)
                    </label>
                    <input
                      id="reset-password"
                      type="password"
                      required
                      minLength={8}
                      value={newPassword}
                      onChange={(e) => setNewPassword(e.target.value)}
                      style={{ width: '100%', marginBottom: 16 }}
                      autoComplete="new-password"
                    />
                    <button type="submit" className="btn btn-primary" disabled={busy}>
                      {busy ? 'Working…' : 'Set new password'}
                    </button>
                  </form>
                )}

                {pending && mode !== 'reset' && (
                  <div style={{ marginTop: 20 }}>
                    <p className="ws-empty">
                      {pending.note ?? 'Verification token issued.'}
                    </p>
                    <label className="ws-empty" htmlFor="verify-token">
                      Verification token
                    </label>
                    <input
                      id="verify-token"
                      type="text"
                      readOnly
                      value={pending.token ?? ''}
                      style={{ width: '100%', marginBottom: 12, fontFamily: 'var(--font-mono-plex), monospace' }}
                      onFocus={(e) => e.currentTarget.select()}
                    />
                    <button type="button" className="btn btn-primary" onClick={onVerify} disabled={busy}>
                      {busy ? 'Working…' : 'Verify this address'}
                    </button>
                  </div>
                )}
              </article>
            )}

            <p className="ws-empty" style={{ marginTop: 24 }}>
              Designs remain yours — see the{' '}
              <Link href="/contact">contact page</Link> for ownership and
              privacy statements.
            </p>
          </div>
        </div>
      </section>
    </main>
  );
}

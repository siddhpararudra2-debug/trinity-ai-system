'use client';

import { useEffect, useRef, useState } from 'react';

import { applyUpdate, checkUpdate, type UpdateCheck } from '@/lib/api/trinity';

/** Poll cadence while open; the updater only answers a small manifest fetch. */
const POLL_MS = 15 * 60 * 1000;

export default function UpdateBanner() {
  const [update, setUpdate] = useState<UpdateCheck | null>(null);
  const [staging, setStaging] = useState(false);
  const [stagedVersion, setStagedVersion] = useState<string | null>(null);
  const [message, setMessage] = useState<string | null>(null);
  const [dismissed, setDismissed] = useState(false);
  // Mirrors of state for the interval callback (refs are only read inside
  // effects, never during render).
  const busyRef = useRef(false);

  useEffect(() => {
    busyRef.current = staging || message !== null;
  }, [staging, message]);

  useEffect(() => {
    let cancelled = false;
    const check = async () => {
      try {
        const info = await checkUpdate();
        if (!cancelled && !busyRef.current) setUpdate(info);
      } catch {
        /* offline or dev server without the route: stay silent */
      }
    };
    void check();
    const timer = window.setInterval(check, POLL_MS);
    return () => {
      cancelled = true;
      window.clearInterval(timer);
    };
  }, []);

  async function onApply() {
    const current = update;
    if (!current?.available) return;
    setStaging(true);
    setMessage(null);
    try {
      const result = await applyUpdate(true);
      setStagedVersion(result.version ?? current.version ?? '');
    } catch (err) {
      setMessage(err instanceof Error ? err.message : 'Update could not be staged.');
      setDismissed(true);
    } finally {
      setStaging(false);
    }
  }

  if (stagedVersion) {
    return (
      <aside className="update-banner" role="status">
        <span className="update-banner-text">
          Trinity AI {stagedVersion} is staged. Close this window and start Trinity again to finish the update.
        </span>
      </aside>
    );
  }
  if (message) {
    return (
      <aside className="update-banner update-banner-warn" role="alert">
        <span className="update-banner-text">{message}</span>
        <button type="button" className="update-banner-close" onClick={() => setDismissed(true)} aria-label="Dismiss">
          ✕
        </button>
      </aside>
    );
  }
  if (dismissed) return null;
  if (update?.available && update.version) {
    return (
      <aside className="update-banner" role="status">
        <span className="update-banner-text">
          Trinity AI {update.version} is available.
        </span>
        <button type="button" className="update-banner-apply" onClick={onApply} disabled={staging}>
          {staging ? 'Staging...' : 'Update'}
        </button>
        <button type="button" className="update-banner-close" onClick={() => setDismissed(true)} aria-label="Not now">
          ✕
        </button>
      </aside>
    );
  }
  return null;
}

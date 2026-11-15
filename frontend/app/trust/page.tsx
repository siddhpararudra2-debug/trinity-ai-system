import type { Metadata } from 'next';
import Link from 'next/link';
import Reveal from '@/components/sections/Reveal';

/**
 * M31 trust pages: privacy, terms, refund & cancellation, design
 * ownership. Every word is served from the deployment's config (the
 * .env.dummy contract — zero code edits) and every policy is marked
 * DRAFT - needs lawyer review. The page degrades to an honest notice
 * when the API is unreachable instead of carrying local copy.
 */

export const metadata: Metadata = {
  title: 'Trust',
  description:
    'Privacy, terms, refund and cancellation, and design ownership — the deployment policy copy, each marked DRAFT pending lawyer review.',
};

const API = process.env.NEXT_PUBLIC_TRINITY_API_URL ?? 'http://localhost:8000/api';

type Policy = {
  slug: string;
  title: string;
  text: string;
  version: string;
  draft: boolean;
  note: string;
};

type Trust = {
  company: string;
  support_email: string;
  policy_version: string;
  draft_note: string;
  policies: Policy[];
};

const policySlugs = [
  { slug: 'privacy', label: 'Privacy policy' },
  { slug: 'terms', label: 'Terms of service' },
  { slug: 'refund-cancellation', label: 'Refund and cancellation' },
  { slug: 'design-ownership', label: 'Design ownership' },
];

async function loadTrust(): Promise<Trust | null> {
  try {
    const response = await fetch(`${API}/trust`, { cache: 'no-store' });
    if (!response.ok) return null;
    return (await response.json()) as Trust;
  } catch {
    return null;
  }
}

export default async function TrustPage() {
  const trust = await loadTrust();

  return (
    <main>
      <section className="page-hero">
        <div className="container">
          <p className="eyebrow">Trust</p>
          <h1>
            Your designs, <span className="accent-word">your terms.</span>
          </h1>
          <p className="lede">
            The four policies this deployment publishes, in the words its
            operator configured — each marked{' '}
            <strong>{trust?.draft_note ?? 'DRAFT - needs lawyer review'}</strong>.
            {trust
              ? ` Policy version ${trust.policy_version}; questions go to ${trust.support_email}.`
              : ' The API is unreachable right now, so no policy text is shown rather than a stale copy.'}
          </p>
          <nav aria-label="Policy contents" style={{ marginTop: 24, display: 'flex', gap: 16, flexWrap: 'wrap' }}>
            {policySlugs.map((item) => (
              <Link key={item.slug} href={`#${item.slug}`} className="btn btn-ghost">
                {item.label}
              </Link>
            ))}
          </nav>
        </div>
      </section>

      <section className="section-sm" aria-label="Policies">
        <div className="container">
          {trust ? (
            trust.policies.map((policy, i) => (
              <Reveal key={policy.slug} delay={i * 60}>
                <article
                  id={policy.slug}
                  className="ws-panel"
                  style={{ marginBottom: 24, scrollMarginTop: 96 }}
                >
                  <div style={{ display: 'flex', gap: 12, alignItems: 'center', flexWrap: 'wrap' }}>
                    <h2 style={{ margin: 0 }}>{policy.title}</h2>
                    <span className="status-pill">{policy.note}</span>
                  </div>
                  <p className="ws-empty">
                    {policy.text
                      ? policy.text
                      : 'The operator has not configured this policy yet — it is served empty rather than filled with borrowed text.'}
                  </p>
                  <p className="ws-empty" style={{ opacity: 0.7 }}>
                    Policy version {policy.version}
                  </p>
                </article>
              </Reveal>
            ))
          ) : (
            <div className="workspace-grid">
              {policySlugs.map((item, i) => (
                <Reveal key={item.slug} delay={i * 60}>
                  <article className="ws-panel" style={{ height: '100%' }}>
                    <h2>{item.label}</h2>
                    <p className="ws-empty">
                      Served by GET /api/trust from deployment config. The API
                      is not reachable from this render, so the text is
                      withheld — never guessed.
                    </p>
                  </article>
                </Reveal>
              ))}
            </div>
          )}

          <Reveal>
            <div style={{ marginTop: 24 }}>
              <Link className="btn btn-ghost" href="/">
                Back to the workspace <span aria-hidden="true">→</span>
              </Link>
            </div>
          </Reveal>
        </div>
      </section>
    </main>
  );
}

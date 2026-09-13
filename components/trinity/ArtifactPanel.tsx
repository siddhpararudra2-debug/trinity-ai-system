'use client';

import type { TrinityResult } from '@/lib/api/trinity';
import { getArtifactUrl } from '@/lib/api/trinity';

function formatBytes(n: number) {
  if (n < 1024) return `${n} B`;
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} KB`;
  return `${(n / (1024 * 1024)).toFixed(2)} MB`;
}

export default function ArtifactPanel({ result, error }: { result: TrinityResult | null; error: string | null }) {
  if (error) {
    return (
      <section className="artifact-section" id="artifacts" aria-labelledby="artifact-title">
        <div className="container">
          <div className="section-label">05 / OUTPUT</div>
          <div style={{ border: '1px solid #E6A8A8', background: '#FDF0F0', padding: 20, display: 'grid', gap: 12 }}>
            <strong style={{ fontFamily: 'var(--font-mono-plex), monospace', fontSize: '0.78rem', letterSpacing: '0.08em', color: '#B42318' }}>
              EXECUTION INTERRUPTED
            </strong>
            <p style={{ margin: 0, fontSize: '0.92rem', color: '#7A2E1E', lineHeight: 1.6 }}>
              TRINITY could not complete this engineering request.
            </p>
            <code style={{ display: 'block', padding: 12, background: '#FFFFFF', border: '1px solid #E6A8A8', fontFamily: 'var(--font-mono-plex), monospace', fontSize: '0.78rem', color: '#B42318' }}>
              {error}
            </code>
            <a href="#workspace" className="btn btn-primary" style={{ width: 'max-content' }}>
              RETRY →
            </a>
          </div>
        </div>
      </section>
    );
  }

  if (!result || !result.artifacts.length) {
    return (
      <section className="artifact-section" id="artifacts" aria-labelledby="artifact-title">
        <div className="container">
          <div className="section-label">05 / OUTPUT</div>
          <h2 id="artifact-title" style={{ margin: '0 0 12px', fontSize: 'clamp(1.8rem, 3.5vw, 3rem)' }}>
            Artifacts are the receipt.
          </h2>
          <p className="lede" style={{ marginBottom: 32 }}>
            Files live on the filesystem; only metadata in SQLite. Awaiting execution —
            demo structure shown.
          </p>
          <div className="artifact-grid">
            {[
              { name: 'TRINITY_FRAME_50MM.GLB', desc: '3D MODEL', size: '1.8 MB' },
              { name: 'TRINITY_FRAME_50MM.STL', desc: 'MANUFACTURING MESH', size: '920 KB' },
              { name: 'SPECIFICATION.JSON', desc: 'ENGINEERING DATA', size: '4 KB' },
            ].map((f) => (
              <div key={f.name} className="artifact-card" style={{ opacity: 0.6 }}>
                <div className="artifact-card-head">
                  <span>ARTIFACT</span>
                  <span>DEMO</span>
                </div>
                <div className="artifact-card-body">
                  <strong>{f.name}</strong>
                  <small>{f.desc}</small>
                  <small style={{ marginTop: 6, opacity: 0.7 }}>{f.size}</small>
                </div>
                <div className="artifact-card-foot">
                  <span className="artifact-size">—</span>
                  <span style={{ fontFamily: 'var(--font-mono-plex), monospace', fontSize: '0.68rem', color: '#707070', letterSpacing: '0.06em' }}>
                    AWAITING
                  </span>
                </div>
              </div>
            ))}
          </div>
        </div>
      </section>
    );
  }

  return (
    <section className="artifact-section" id="artifacts" aria-labelledby="artifact-title">
      <div className="container">
        <div className="section-label">05 / OUTPUT — LIVE RESULT</div>
        <h2 id="artifact-title" style={{ margin: '0 0 12px', fontSize: 'clamp(1.8rem, 3.5vw, 3rem)' }}>
          Every artifact has provenance.
        </h2>
        <p className="lede" style={{ marginBottom: 32 }}>
          Server-generated IDs, SHA-256 checksums and SQLite lineage behind every
          file. <span className="mono" style={{ fontSize: '0.78rem' }}>JOB {result.job_id.slice(0, 8)}</span> · {result.artifacts.length} files.
        </p>

        <div className="artifact-grid">
          {result.artifacts.map((a) => (
            <article key={a.artifact_id} className="artifact-card">
              <div className="artifact-card-head">
                <span>ARTIFACT · {a.type.toUpperCase()}</span>
                <span className="status-pill is-live" style={{ padding: '3px 8px', fontSize: '0.54rem' }}>
                  {a.checksum.slice(0, 8)}
                </span>
              </div>
              <div className="artifact-card-body">
                <strong>TRINITY_{a.type.toUpperCase()}_{a.artifact_id.slice(0, 6)}. {a.type.toUpperCase()}</strong>
                <small>{a.type.toUpperCase()} · {a.path.split('/').pop()?.toUpperCase() ?? a.type.toUpperCase()}</small>
                <small style={{ marginTop: 6, opacity: 0.7 }}>{formatBytes(a.size_bytes)} · SHA-256</small>
              </div>
              <div className="artifact-card-foot">
                <span className="artifact-size">{formatBytes(a.size_bytes)}</span>
                <a className="btn-download" href={getArtifactUrl(a.artifact_id)} target="_blank" rel="noopener noreferrer" download>
                  DOWNLOAD
                </a>
              </div>
            </article>
          ))}
        </div>

        <div style={{ marginTop: 20, padding: 14, border: '1px solid #D0D0CA', background: '#F7F7F4', display: 'flex', justifyContent: 'space-between', gap: 12, flexWrap: 'wrap', fontFamily: 'var(--font-mono-plex), monospace', fontSize: '0.66rem', letterSpacing: '0.06em', color: '#707070' }}>
          <span>CHECKSUMS VERIFIED · SQLITE LINEAGE · NO CACHE REUSE FOR CAD</span>
          <span>JOB {result.job_id}</span>
        </div>
      </div>
    </section>
  );
}

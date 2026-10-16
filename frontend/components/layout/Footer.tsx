import Link from 'next/link';

const engineLinks = [
  { href: '/', label: 'Workspace' },
  { href: '/systems', label: 'Systems' },
  { href: '/projects', label: 'Artifacts & Jobs' },
  { href: '/chat', label: 'Chat' },
  { href: '/about', label: 'About' },
];

const systemLinks = [
  { href: '/services', label: 'Engine registry' },
  { href: '/about', label: 'Architecture' },
  { href: 'https://github.com/siddhpararudra2-debug/trinity-ai-system', label: 'GitHub' },
  { href: '/contact', label: 'Contact' },
];

export default function Footer() {
  const currentYear = new Date().getFullYear();

  return (
    <footer className="footer">
      <div className="container">
        <div className="footer-top">
          <div>
            <div className="footer-display" aria-hidden="true">
              TRINITY
            </div>
            <div
              style={{
                marginTop: 10,
                fontFamily: 'var(--font-mono-plex), monospace',
                fontSize: '0.7rem',
                letterSpacing: '0.16em',
                color: 'rgba(255,255,255,0.5)',
              }}
            >
              ENGINEERING INTELLIGENCE / 2026
            </div>
          </div>
          <Link href="/" className="btn btn-primary">
            OPEN WORKSPACE
          </Link>
        </div>

        <div className="footer-grid">
          <div>
            <Link href="/" className="brand" aria-label="Trinity AI home">
              <span className="brand-mark" aria-hidden="true">
                ◈
              </span>
              <span className="brand-text">
                TRINITY
                <small>AI ENGINEERING SYSTEM</small>
              </span>
            </Link>
            <p className="footer-note" style={{ marginTop: 18 }}>
              An LLM-independent engineering operating system. Every engine call returns the same
              envelope — result, artifacts, validation — with SQLite lineage behind each job.
            </p>
          </div>

          <div>
            <h4 className="footer-heading">Explore</h4>
            <ul className="footer-links">
              {engineLinks.map((link) => (
                <li key={link.href}>
                  {link.href.startsWith('http') ? (
                    <a href={link.href} target="_blank" rel="noopener noreferrer">
                      {link.label}
                    </a>
                  ) : (
                    <Link href={link.href}>{link.label}</Link>
                  )}
                </li>
              ))}
            </ul>
          </div>

          <div>
            <h4 className="footer-heading">System</h4>
            <ul className="footer-links">
              {systemLinks.map((link) => (
                <li key={link.href}>
                  {link.href.startsWith('http') ? (
                    <a href={link.href} target="_blank" rel="noopener noreferrer">
                      {link.label}
                    </a>
                  ) : (
                    <Link href={link.href}>{link.label}</Link>
                  )}
                </li>
              ))}
            </ul>
          </div>
        </div>

        <div className="footer-bottom">
          <span>BUILT FOR ENGINEERS — DETERMINISTIC CORE · INDEPENDENT VALIDATION · SQLITE LINEAGE</span>
          <span>© {currentYear} TRINITY AI — V1</span>
        </div>
      </div>
    </footer>
  );
}

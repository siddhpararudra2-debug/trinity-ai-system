'use client';

import { useEffect, useState } from 'react';
import Link from 'next/link';
import { usePathname } from 'next/navigation';

const links = [
  { href: '#workspace', label: 'Workspace' },
  { href: '#systems', label: 'Systems' },
  { href: '#artifacts', label: 'Artifacts' },
  { href: '#projects', label: 'Projects' },
  { href: '#about', label: 'About' },
];

export default function Navigation() {
  const [scrolled, setScrolled] = useState(false);
  const [open, setOpen] = useState(false);
  const pathname = usePathname();

  useEffect(() => {
    const onScroll = () => setScrolled(window.scrollY > 12);
    onScroll();
    window.addEventListener('scroll', onScroll, { passive: true });
    return () => window.removeEventListener('scroll', onScroll);
  }, []);

  return (
    <nav
      className={`nav-wrap ${scrolled ? 'is-scrolled' : ''}`}
      aria-label="Primary navigation"
    >
      <div className="container nav">
        <Link href="/" className="brand" aria-label="Trinity home">
          <span className="brand-mark" aria-hidden="true">◈</span>
          <span className="brand-text">
            TRINITY
            <small>AI ENGINEERING SYSTEM</small>
          </span>
        </Link>

        <div className="nav-links">
          {links.map((l) => (
            <Link
              key={l.href}
              href={l.href.startsWith('#') ? `/${l.href}` : l.href}
              className={pathname === l.href ? 'is-active' : ''}
            >
              {l.label}
            </Link>
          ))}
        </div>

        <div className="nav-meta">
          <span className="system-ready" aria-label="System status">
            <i aria-hidden="true" />
            SYSTEM READY
          </span>
          <Link href="#workspace" className="btn btn-primary">
            OPEN WORKSPACE
          </Link>
        </div>

        <button
          className="mobile-toggle"
          onClick={() => setOpen(!open)}
          aria-label={open ? 'Close menu' : 'Open menu'}
          aria-expanded={open}
          type="button"
        >
          {open ? '×' : '≡'}
        </button>
      </div>

      <div className="mobile-menu" hidden={!open}>
        {links.map((l) => (
          <Link
            key={l.href}
            href={l.href.startsWith('#') ? `/${l.href}` : l.href}
            onClick={() => setOpen(false)}
          >
            {l.label}
          </Link>
        ))}
        <Link href="#workspace" onClick={() => setOpen(false)}>
          Open Workspace
        </Link>
      </div>
    </nav>
  );
}

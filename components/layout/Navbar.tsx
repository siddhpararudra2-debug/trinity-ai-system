'use client';

import { useEffect, useState } from 'react';
import Link from 'next/link';
import { usePathname } from 'next/navigation';

const navLinks = [
  { href: '/#workspace', label: 'Workspace' },
  { href: '/#systems', label: 'Systems' },
  { href: '/#artifacts', label: 'Artifacts' },
  { href: '/#projects', label: 'Projects' },
  { href: '/#about', label: 'About' },
];

export default function Navbar() {
  const [mobileOpen, setMobileOpen] = useState(false);
  const [scrolled, setScrolled] = useState(false);
  const pathname = usePathname();
  const closeMenu = () => setMobileOpen(false);

  useEffect(() => {
    const onScroll = () => setScrolled(window.scrollY > 12);
    onScroll();
    window.addEventListener('scroll', onScroll, { passive: true });
    return () => window.removeEventListener('scroll', onScroll);
  }, []);

  return (
    <nav className={`nav-wrap ${scrolled ? 'is-scrolled' : ''}`} id="main-nav" aria-label="Primary navigation">
      <div className="container nav">
        <Link href="/" className="brand" aria-label="Trinity AI home">
          <span className="brand-mark" aria-hidden="true">
            ◈
          </span>
          <span className="brand-text">
            TRINITY
            <small>AI ENGINEERING SYSTEM</small>
          </span>
        </Link>

        <div className="nav-links">
          {navLinks.map((link) => (
            <Link
              key={link.href}
              href={link.href}
              className={pathname === link.href ? 'is-active' : ''}
            >
              {link.label}
            </Link>
          ))}
        </div>

        <div className="nav-meta">
          <span className="system-ready" aria-label="System status">
            <i aria-hidden="true" />
            SYSTEM READY
          </span>
          <Link href="/#workspace" className="btn btn-primary">
            OPEN WORKSPACE
          </Link>
        </div>

        <button
          className="mobile-toggle"
          onClick={() => setMobileOpen(!mobileOpen)}
          aria-label={mobileOpen ? 'Close menu' : 'Open menu'}
          aria-expanded={mobileOpen}
        >
          {mobileOpen ? '×' : '≡'}
        </button>
      </div>

      <div className="mobile-menu" hidden={!mobileOpen}>
        {navLinks.map((link) => (
          <Link key={link.href} href={link.href} onClick={closeMenu}>
            {link.label}
          </Link>
        ))}
        <Link href="/#workspace" onClick={closeMenu}>
          Open Workspace
        </Link>
      </div>
    </nav>
  );
}

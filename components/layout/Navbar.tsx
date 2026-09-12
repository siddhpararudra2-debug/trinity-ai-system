'use client';

import { useState, useEffect } from 'react';
import Link from 'next/link';
import { usePathname } from 'next/navigation';

const navLinks = [
  { href: '/', label: 'Home' },
  { href: '/about', label: 'About' },
  { href: '/services', label: 'Services' },
  { href: '/projects', label: 'Projects' },
  { href: '/team', label: 'Team' },
  { href: '/contact', label: 'Contact' },
];

export default function Navbar() {
  const [scrolled, setScrolled] = useState(false);
  const [mobileOpen, setMobileOpen] = useState(false);
  const pathname = usePathname();

  useEffect(() => {
    const handleScroll = () => {
      setScrolled(window.scrollY > 50);
    };
    window.addEventListener('scroll', handleScroll);
    handleScroll();
    return () => window.removeEventListener('scroll', handleScroll);
  }, []);

  const isHome = pathname === '/';
  const navClass = `navbar ${scrolled || !isHome ? 'navbar--scrolled' : ''}`;

  return (
    <nav className={navClass} id="main-nav">
      <div className="container navbar__inner">
        <Link href="/" className="navbar__logo" aria-label="TRINITY Systems Home">
          <span className="navbar__logo-icon">T</span>
          TRINITY
        </Link>

        <div className="navbar__links">
          {navLinks.map((link) => (
            <Link
              key={link.href}
              href={link.href}
              className={`navbar__link ${pathname === link.href ? 'navbar__link--active' : ''}`}
            >
              {link.label}
            </Link>
          ))}
          <Link href="/contact" className="navbar__cta">
            Get a Quote
          </Link>
        </div>

        <button
          className="navbar__mobile-toggle"
          onClick={() => setMobileOpen(!mobileOpen)}
          aria-label="Toggle navigation menu"
          aria-expanded={mobileOpen}
          id="mobile-nav-toggle"
        >
          <span />
          <span />
          <span />
        </button>
      </div>

      <div className={`navbar__mobile-menu ${mobileOpen ? 'navbar__mobile-menu--open' : ''}`}>
        {navLinks.map((link) => (
          <Link
            key={link.href}
            href={link.href}
            onClick={() => setMobileOpen(false)}
            className={`navbar__link ${pathname === link.href ? 'navbar__link--active' : ''}`}
          >
            {link.label}
          </Link>
        ))}
        <Link href="/contact" className="btn btn--primary" style={{ textAlign: 'center' }} onClick={() => setMobileOpen(false)}>
          Get a Quote
        </Link>
      </div>
    </nav>
  );
}

import type { Metadata } from 'next';
import { Inter } from 'next/font/google';
import './globals.css';

const inter = Inter({
  subsets: ['latin'],
  display: 'swap',
  variable: '--font-inter',
});

export const metadata: Metadata = {
  title: {
    default: 'Trinity AI — Engineering Operating System',
    template: '%s | Trinity AI',
  },
  description:
    'TRINITY Systems delivers world-class precision engineering, industrial automation, and manufacturing solutions. 25+ years of engineering excellence serving 12 countries worldwide.',
  keywords: [
    'precision engineering',
    'industrial automation',
    'manufacturing solutions',
    'quality assurance',
    'R&D consulting',
    'TRINITY Systems',
  ],
  openGraph: {
    type: 'website',
    locale: 'en_IN',
    siteName: 'TRINITY Systems',
    title: 'TRINITY Systems — Precision Engineering & Manufacturing Solutions',
    description:
      'World-class precision engineering, industrial automation, and manufacturing solutions. 25+ years of engineering excellence.',
  },
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="en" className={inter.variable}>
      <body style={{ fontFamily: 'var(--font-inter), var(--font-sans)' }}>{children}</body>
    </html>
  );
}

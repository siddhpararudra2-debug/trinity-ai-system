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
  description: 'Trinity AI is an engineering operating system for deterministic, validated artifacts.',
  keywords: [
    'engineering operating system',
    'parametric CAD',
    'validated engineering artifacts',
    'Trinity AI',
  ],
  openGraph: {
    type: 'website',
    locale: 'en_IN',
    siteName: 'Trinity AI',
    title: 'Trinity AI — Engineering Operating System',
    description: 'Deterministic engineering tools that generate and validate artifacts.',
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

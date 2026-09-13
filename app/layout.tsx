import type { Metadata } from 'next';
import { Space_Grotesk, JetBrains_Mono, Inter, IBM_Plex_Mono } from 'next/font/google';
import Navbar from '@/components/layout/Navbar';
import Footer from '@/components/layout/Footer';
import './globals.css';

const spaceGrotesk = Space_Grotesk({
  subsets: ['latin'],
  display: 'swap',
  weight: ['400', '500', '600', '700'],
  variable: '--font-display',
});

const inter = Inter({
  subsets: ['latin'],
  display: 'swap',
  weight: ['400', '500', '600'],
  variable: '--font-body',
});

const jetbrainsMono = JetBrains_Mono({
  subsets: ['latin'],
  display: 'swap',
  weight: ['400', '600'],
  variable: '--font-mono',
});

const ibmPlexMono = IBM_Plex_Mono({
  subsets: ['latin'],
  display: 'swap',
  weight: ['400', '500', '600'],
  variable: '--font-mono-plex',
});

export const metadata: Metadata = {
  title: {
    default: 'Trinity AI — Engineering Operating System',
    template: '%s | Trinity AI',
  },
  description:
    'Trinity AI is an LLM-independent engineering operating system: models may reason later, while deterministic tools execute and independent checks verify.',
  keywords: [
    'engineering operating system',
    'deterministic CAD',
    'validated engineering artifacts',
    'engine registry',
    'Trinity AI',
  ],
  openGraph: {
    type: 'website',
    locale: 'en_IN',
    siteName: 'Trinity AI',
    title: 'Trinity AI — Engineering Operating System',
    description: 'Deterministic engines generate and validate engineering artifacts with full provenance.',
  },
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="en" className={`${spaceGrotesk.variable} ${inter.variable} ${jetbrainsMono.variable} ${ibmPlexMono.variable}`}>
      <body>
        <Navbar />
        {children}
        <Footer />
      </body>
    </html>
  );
}

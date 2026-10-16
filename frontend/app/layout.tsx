import type { Metadata } from 'next';
import Navbar from '@/components/layout/Navbar';
import Footer from '@/components/layout/Footer';
import { ThemeProvider } from '@/components/trinity/ThemeProvider';
import './globals.css';

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
    images: [
      {
        url: '/og-image.png',
        width: 1200,
        height: 630,
        alt: 'Trinity AI — Design. Simulate. Validate. Deterministically.',
      },
    ],
  },
  twitter: {
    card: 'summary_large_image',
    title: 'Trinity AI — Engineering Operating System',
    description: 'Deterministic engines generate and validate engineering artifacts with full provenance.',
    images: ['/og-image.png'],
  },
};

export const viewport = {
  themeColor: '#111111',
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="en" suppressHydrationWarning>
      <body>
        {/* Theme bootstrap: runs before paint so the stored/OS theme
            applies without a flash. Mirrored by ThemeProvider. */}
        <script
          dangerouslySetInnerHTML={{
            __html:
              "(function(){try{var t=localStorage.getItem('trinity-theme');if(!t){t=matchMedia('(prefers-color-scheme: dark)').matches?'dark':'light';}document.documentElement.dataset.theme=t;}catch(e){}})();",
          }}
        />
        <ThemeProvider>
          <Navbar />
          {children}
          <Footer />
        </ThemeProvider>
      </body>
    </html>
  );
}

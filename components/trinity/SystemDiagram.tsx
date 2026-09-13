/* eslint-disable react-hooks/set-state-in-effect */
'use client';

import { useEffect, useState } from 'react';
import { motion, useReducedMotion } from 'framer-motion';

type Props = {
  inputs: string[];
  outputs: string[];
  center: [string, string];
};

export default function SystemDiagram({ inputs, outputs, center }: Props) {
  const shouldReduce = useReducedMotion();
  const [mounted, setMounted] = useState(false);
  useEffect(() => setMounted(true), []);

  const pathsIn = [
    'M 26 18 C 90 18, 110 60, 128 60',
    'M 26 60 C 90 60, 110 60, 128 60',
    'M 26 102 C 90 102, 110 60, 128 60',
  ];
  const pathsOut = [
    'M 212 60 C 232 60, 252 18, 314 18',
    'M 212 60 C 232 60, 252 60, 314 60',
    'M 212 60 C 232 60, 252 102, 314 102',
  ];

  return (
    <div className="system-diagram-wrap" aria-hidden="true">
      <svg viewBox="0 0 340 120" preserveAspectRatio="none">
        {pathsIn.map((d, i) => (
          <path key={`in-${i}`} d={d} className={i === 1 ? 'is-accent' : ''} />
        ))}
        <rect x={128} y={32} width={84} height={56} rx={4} fill="#FFFFFF" stroke="#111111" strokeWidth={0.9} />
        <text x={170} y={58} textAnchor="middle" fontFamily="monospace" fontSize={6.5} fontWeight={700} letterSpacing={0.6} fill="#111111">
          {center[0]}
        </text>
        <text x={170} y={68} textAnchor="middle" fontFamily="monospace" fontSize={5} letterSpacing={0.8} fill="#5A5A56">
          {center[1]}
        </text>
        {pathsOut.map((d, i) => (
          <path key={`out-${i}`} d={d} className={i === 1 ? 'is-accent' : ''} />
        ))}

        {/* particles — CSS fallback if reduced motion or not mounted */}
        {mounted && !shouldReduce && (
          <>
            {pathsIn.map((d, i) => (
              <motion.circle
                key={`p-in-${i}`}
                r={1.7}
                fill={i === 1 ? '#315B73' : '#9A9A94'}
                initial={{ offsetDistance: '0%' }}
                animate={{ offsetDistance: '100%' }}
                transition={{ duration: 2.8 + i * 0.6, repeat: Infinity, ease: 'linear', delay: i * 0.4 }}
                style={{ offsetPath: `path("${d}")` } as unknown as React.CSSProperties}
              />
            ))}
            {pathsOut.map((d, i) => (
              <motion.circle
                key={`p-out-${i}`}
                r={1.7}
                fill={i === 1 ? '#315B73' : '#9A9A94'}
                initial={{ offsetDistance: '0%' }}
                animate={{ offsetDistance: '100%' }}
                transition={{ duration: 2.6 + i * 0.5, repeat: Infinity, ease: 'linear', delay: 0.8 + i * 0.3 }}
                style={{ offsetPath: `path("${d}")` } as unknown as React.CSSProperties}
              />
            ))}
          </>
        )}

        {inputs.slice(0, 3).map((label, i) => (
          <text key={label} x={2} y={21 + i * 42} fontFamily="monospace" fontSize={5} letterSpacing={0.5} fill="#5A5A56">
            {label}
          </text>
        ))}
        {outputs.slice(0, 3).map((label, i) => (
          <text key={label} x={318} y={21 + i * 42} textAnchor="end" fontFamily="monospace" fontSize={5} letterSpacing={0.5} fill="#315B73">
            {label}
          </text>
        ))}
      </svg>
    </div>
  );
}

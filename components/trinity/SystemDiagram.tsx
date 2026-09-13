'use client';

type Props = {
  inputs: string[];
  outputs: string[];
  center: [string, string];
  accent?: boolean;
};

export default function SystemDiagram({ inputs, outputs, center }: Props) {
  return (
    <div className="system-diagram-wrap" aria-hidden="true">
      <svg viewBox="0 0 340 120" preserveAspectRatio="none">
        {/* input lines */}
        {inputs.slice(0, 3).map((_, i) => {
          const y1 = 18 + i * 42;
          const y2 = 60;
          return (
            <g key={i}>
              <path d={`M 26 ${y1} C 90 ${y1}, 110 ${y2}, 128 ${y2}`} className={i === 1 ? 'is-accent' : ''} />
              <circle r={1.6} fill={i === 1 ? '#315B73' : '#C8C8C0'}>
                <animateMotion dur={`${2.8 + i * 0.6}s`} repeatCount="indefinite" path={`M 26 ${y1} C 90 ${y1}, 110 ${y2}, 128 ${y2}`} />
              </circle>
            </g>
          );
        })}
        {/* core */}
        <rect x={128} y={32} width={84} height={56} rx={4} fill="#FFFFFF" stroke="#111111" strokeWidth={0.9} />
        <text x={170} y={58} textAnchor="middle" fontFamily="monospace" fontSize={6.5} fontWeight={700} letterSpacing={0.6} fill="#111111">
          {center[0]}
        </text>
        <text x={170} y={68} textAnchor="middle" fontFamily="monospace" fontSize={5} letterSpacing={0.8} fill="#707070">
          {center[1]}
        </text>
        {/* output lines */}
        {outputs.slice(0, 3).map((_, i) => {
          const y1 = 60;
          const y2 = 18 + i * 42;
          return (
            <g key={i}>
              <path d={`M 212 ${y1} C 232 ${y1}, 252 ${y2}, 314 ${y2}`} className={i === 1 ? 'is-accent' : ''} />
              <circle r={1.6} fill={i === 1 ? '#315B73' : '#C8C8C0'}>
                <animateMotion dur={`${2.6 + i * 0.5}s`} repeatCount="indefinite" path={`M 212 ${y1} C 232 ${y1}, 252 ${y2}, 314 ${y2}`} />
              </circle>
            </g>
          );
        })}
        {/* input labels */}
        {inputs.slice(0, 3).map((label, i) => (
          <text key={label} x={2} y={21 + i * 42} fontFamily="monospace" fontSize={5} letterSpacing={0.5} fill="#707070">
            {label}
          </text>
        ))}
        {/* output labels */}
        {outputs.slice(0, 3).map((label, i) => (
          <text key={label} x={318} y={21 + i * 42} textAnchor="end" fontFamily="monospace" fontSize={5} letterSpacing={0.5} fill="#315B73">
            {label}
          </text>
        ))}
      </svg>
    </div>
  );
}
